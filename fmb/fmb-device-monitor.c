/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2009-2010 Jannis Pohlmann <jannis@xfce.org>
 * Copyright (c) 2012      Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixmounts.h>
#endif

#include <fmb/fmb-device-monitor.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-marshal.h>
#include <fmb/fmb-preferences.h>



/* signal identifiers */
enum
{
  DEVICE_ADDED,
  DEVICE_REMOVED,
  DEVICE_CHANGED,
  DEVICE_PRE_UNMOUNT,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_HIDDEN_DEVICES
};



static void           fmb_device_monitor_finalize               (GObject                *object);
static void           fmb_device_monitor_get_property           (GObject                *object,
                                                                    guint                   prop_id,
                                                                    GValue                 *value,
                                                                    GParamSpec             *pspec);
static void           fmb_device_monitor_set_property           (GObject                *object,
                                                                    guint                   prop_id,
                                                                    const GValue           *value,
                                                                    GParamSpec             *pspec);
static void           fmb_device_monitor_update_hidden          (gpointer                key,
                                                                    gpointer                value,
                                                                    gpointer                data);
static void           fmb_device_monitor_volume_added           (GVolumeMonitor         *volume_monitor,
                                                                    GVolume                *volume,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_volume_removed         (GVolumeMonitor         *volume_monitor,
                                                                    GVolume                *volume,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_volume_changed         (GVolumeMonitor         *volume_monitor,
                                                                    GVolume                *volume,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_mount_added            (GVolumeMonitor         *volume_monitor,
                                                                    GMount                 *mount,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_mount_removed          (GVolumeMonitor         *volume_monitor,
                                                                    GMount                 *mount,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_mount_changed          (GVolumeMonitor         *volume_monitor,
                                                                    GMount                 *mount,
                                                                    FmbDeviceMonitor    *monitor);
static void           fmb_device_monitor_mount_pre_unmount      (GVolumeMonitor         *volume_monitor,
                                                                    GMount                 *mount,
                                                                    FmbDeviceMonitor    *monitor);



struct _FmbDeviceMonitorClass
{
  GObjectClass __parent__;

  /* signals */
  void (*device_added)       (FmbDeviceMonitor *monitor,
                              FmbDevice        *device);
  void (*device_removed)     (FmbDeviceMonitor *monitor,
                              FmbDevice        *device);
  void (*device_changed)     (FmbDeviceMonitor *monitor,
                              FmbDevice        *device);
  void (*device_pre_unmount) (FmbDeviceMonitor *monitor,
                              FmbDevice        *device,
                              GFile               *root_file);
};

struct _FmbDeviceMonitor
{
  GObject __parent__;

  GVolumeMonitor     *volume_monitor;

  /* GVolume/GMount -> FmbDevice */
  GHashTable         *devices;

  /* GVolumes from GVolumeMonitor that are currently invisible */
  GList              *hidden_volumes;

  /* user defined hidden volumes */
  FmbPreferences  *preferences;
  gchar             **hidden_devices;
};



static guint device_monitor_signals[LAST_SIGNAL];



G_DEFINE_TYPE (FmbDeviceMonitor, fmb_device_monitor, G_TYPE_OBJECT)



static void
fmb_device_monitor_class_init (FmbDeviceMonitorClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_device_monitor_finalize;
  gobject_class->get_property = fmb_device_monitor_get_property;
  gobject_class->set_property = fmb_device_monitor_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_HIDDEN_DEVICES,
                                   g_param_spec_boxed ("hidden-devices",
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_STRV,
                                                       BLXO_PARAM_READWRITE));

  device_monitor_signals[DEVICE_ADDED] =
      g_signal_new (I_("device-added"),
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (FmbDeviceMonitorClass, device_added),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT,
                    G_TYPE_NONE, 1, G_TYPE_OBJECT);

  device_monitor_signals[DEVICE_REMOVED] =
      g_signal_new (I_("device-removed"),
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (FmbDeviceMonitorClass, device_removed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT,
                    G_TYPE_NONE, 1, G_TYPE_OBJECT);

  device_monitor_signals[DEVICE_CHANGED] =
      g_signal_new (I_("device-changed"),
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (FmbDeviceMonitorClass, device_changed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT,
                    G_TYPE_NONE, 1, G_TYPE_OBJECT);

  device_monitor_signals[DEVICE_PRE_UNMOUNT] =
      g_signal_new (I_("device-pre-unmount"),
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (FmbDeviceMonitorClass, device_pre_unmount),
                    NULL, NULL,
                    _fmb_marshal_VOID__OBJECT_OBJECT,
                    G_TYPE_NONE, 2,
                    G_TYPE_OBJECT, G_TYPE_OBJECT);
}



static void
fmb_device_monitor_init (FmbDeviceMonitor *monitor)
{
  GList *list;
  GList *lp;

  monitor->preferences = fmb_preferences_get ();
  blxo_binding_new (G_OBJECT (monitor->preferences), "hidden-devices",
                   G_OBJECT (monitor), "hidden-devices");

  /* table for GVolume/GMount (key) -> FmbDevice (value) */
  monitor->devices = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);

  /* gio volume monitor */
  monitor->volume_monitor = g_volume_monitor_get ();

  /* load all volumes */
  list = g_volume_monitor_get_volumes (monitor->volume_monitor);
  for (lp = list; lp != NULL; lp = lp->next)
    {
      fmb_device_monitor_volume_added (monitor->volume_monitor, lp->data, monitor);
      g_object_unref (G_OBJECT (lp->data));
    }
  g_list_free (list);

  /* load all mount */
  list = g_volume_monitor_get_mounts (monitor->volume_monitor);
  for (lp = list; lp != NULL; lp = lp->next)
    {
      fmb_device_monitor_mount_added (monitor->volume_monitor, lp->data, monitor);
      g_object_unref (G_OBJECT (lp->data));
    }
  g_list_free (list);

  /* watch changes */
  g_signal_connect (monitor->volume_monitor, "volume-added", G_CALLBACK (fmb_device_monitor_volume_added), monitor);
  g_signal_connect (monitor->volume_monitor, "volume-removed", G_CALLBACK (fmb_device_monitor_volume_removed), monitor);
  g_signal_connect (monitor->volume_monitor, "volume-changed", G_CALLBACK (fmb_device_monitor_volume_changed), monitor);
  g_signal_connect (monitor->volume_monitor, "mount-added", G_CALLBACK (fmb_device_monitor_mount_added), monitor);
  g_signal_connect (monitor->volume_monitor, "mount-removed", G_CALLBACK (fmb_device_monitor_mount_removed), monitor);
  g_signal_connect (monitor->volume_monitor, "mount-changed", G_CALLBACK (fmb_device_monitor_mount_changed), monitor);
  g_signal_connect (monitor->volume_monitor, "mount-pre-unmount", G_CALLBACK (fmb_device_monitor_mount_pre_unmount), monitor);
}



static void
fmb_device_monitor_finalize (GObject *object)
{
  FmbDeviceMonitor *monitor = FMB_DEVICE_MONITOR (object);

  /* release properties */
  g_object_unref (monitor->preferences);
  g_strfreev (monitor->hidden_devices);

  /* detatch from the monitor */
  g_signal_handlers_disconnect_matched (monitor->volume_monitor, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, monitor);
  g_object_unref (monitor->volume_monitor);

  /* clear list of devices */
  g_hash_table_destroy (monitor->devices);

  /* clear list of hidden volumes */
  g_list_free_full (monitor->hidden_volumes, g_object_unref);

  (*G_OBJECT_CLASS (fmb_device_monitor_parent_class)->finalize) (object);
}



static void
fmb_device_monitor_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  FmbDeviceMonitor *monitor = FMB_DEVICE_MONITOR (object);

  switch (prop_id)
    {
    case PROP_HIDDEN_DEVICES:
      g_value_set_boxed (value, monitor->hidden_devices);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_device_monitor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  FmbDeviceMonitor *monitor = FMB_DEVICE_MONITOR (object);

  switch (prop_id)
    {
    case PROP_HIDDEN_DEVICES:
      /* set */
      g_strfreev (monitor->hidden_devices);
      monitor->hidden_devices = g_value_dup_boxed (value);

      /* update the devices */
      if (monitor->devices != NULL)
        g_hash_table_foreach (monitor->devices, fmb_device_monitor_update_hidden, monitor);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gboolean
fmb_device_monitor_id_is_hidden (FmbDeviceMonitor *monitor,
                                    const gchar         *id)
{
  guint n;

  if (id == NULL || monitor->hidden_devices == NULL)
    return FALSE;

  /* check if the uuid is in the hidden list */
  for (n = 0; monitor->hidden_devices[n] != NULL; n++)
    if (g_strcmp0 (monitor->hidden_devices[n], id)== 0)
      return TRUE;

  return FALSE;
}



static void
fmb_device_monitor_update_hidden (gpointer key,
                                     gpointer value,
                                     gpointer data)
{
  FmbDeviceMonitor *monitor = FMB_DEVICE_MONITOR (data);
  FmbDevice        *device = FMB_DEVICE (value);
  gchar               *id;
  gboolean             hidden;

  /* get state of the device */
  id = fmb_device_get_identifier (device);
  hidden = fmb_device_monitor_id_is_hidden (monitor, id);
  g_free (id);

  if (fmb_device_get_hidden (device) != hidden)
    {
      g_object_set (G_OBJECT (device), "hidden", hidden, NULL);
      g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_CHANGED], 0, device);
    }
}



static gboolean
fmb_device_monitor_volume_is_visible (GVolume *volume)
{
  gboolean         can_eject = FALSE;
  gboolean         can_mount = FALSE;
  gboolean         can_unmount = FALSE;
  gboolean         is_removable = FALSE;
  GDrive          *drive;
  GMount          *mount;

  _fmb_return_val_if_fail (G_IS_VOLUME (volume), TRUE);

  /* determine the mount for the volume (if it is mounted at all) */
  mount = g_volume_get_mount (volume);
  if (mount != NULL)
    {
      /* check if the volume can be unmounted */
      can_unmount = g_mount_can_unmount (mount);

      /* release the mount */
      g_object_unref (mount);
    }

  /* check if the volume can be ejected */
  can_eject = g_volume_can_eject (volume);

  /* determine the drive for the volume */
  drive = g_volume_get_drive (volume);
  if (drive != NULL)
    {
      /* check if the drive media can be removed */
      is_removable = g_drive_is_media_removable (drive);

      /* release the drive */
      g_object_unref (drive);
    }

  /* determine whether the device can be mounted */
  can_mount = g_volume_can_mount (volume);

  return (can_eject || can_unmount || is_removable || can_mount);
}



static void
fmb_device_monitor_volume_added (GVolumeMonitor      *volume_monitor,
                                    GVolume             *volume,
                                    FmbDeviceMonitor *monitor)
{
  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_VOLUME (volume));

  /* check that the volume is not in the internal list already */
  if (g_list_find (monitor->hidden_volumes, volume) != NULL)
    return;

  /* nor in the list of visible volumes */
  if (g_hash_table_lookup (monitor->devices, volume) != NULL)
    return;

  /* add to internal list */
  monitor->hidden_volumes = g_list_prepend (monitor->hidden_volumes, g_object_ref (volume));

  /* change visibility in changed */
  fmb_device_monitor_volume_changed (volume_monitor, volume, monitor);
}



static void
fmb_device_monitor_volume_removed (GVolumeMonitor      *volume_monitor,
                                      GVolume             *volume,
                                      FmbDeviceMonitor *monitor)
{
  FmbDevice *device;
  GList        *lp;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_VOLUME (volume));

  /* remove the volume */
  lp = g_list_find (monitor->hidden_volumes, volume);
  if (lp != NULL)
    {
      /* silently drop it */
      monitor->hidden_volumes = g_list_delete_link (monitor->hidden_volumes, lp);

      /* release ref from hidden list */
      g_object_unref (G_OBJECT (volume));
    }
  else
    {
      /* find device */
      device = g_hash_table_lookup (monitor->devices, volume);

      /* meh */
      _fmb_return_if_fail (FMB_IS_DEVICE (device));
      if (G_UNLIKELY (device == NULL))
        return;

      /* the device is not visble for the user */
      g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_REMOVED], 0, device);

      /* drop it */
      g_hash_table_remove (monitor->devices, volume);
    }
}



static void
fmb_device_monitor_volume_changed (GVolumeMonitor      *volume_monitor,
                                      GVolume             *volume,
                                      FmbDeviceMonitor *monitor)
{
  GList        *lp;
  FmbDevice *device;
  gchar        *id;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_VOLUME (volume));

  lp = g_list_find (monitor->hidden_volumes, volume);
  if (lp != NULL)
    {
      /* check if the volume should be visible again */
      if (fmb_device_monitor_volume_is_visible (volume))
        {
          /* remove from the hidden list */
          monitor->hidden_volumes = g_list_delete_link (monitor->hidden_volumes, lp);

          /* create a new device for this volume */
          device = g_object_new (FMB_TYPE_DEVICE,
                                 "device", volume,
                                 "kind", FMB_DEVICE_KIND_VOLUME,
                                 NULL);

          /* set visibility */
          id = fmb_device_get_identifier (device);
          g_object_set (G_OBJECT (device),
                        "hidden", fmb_device_monitor_id_is_hidden (monitor, id),
                        NULL);
          g_free (id);

          /* insert to list (takes ref from hidden list) */
          g_hash_table_insert (monitor->devices, volume, device);

          /* notify */
          g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_ADDED], 0, device);
        }
    }
  else
    {
      /* find device */
      device = g_hash_table_lookup (monitor->devices, volume);

      /* meh */
      _fmb_return_if_fail (FMB_IS_DEVICE (device));
      if (G_UNLIKELY (device == NULL))
        return;

      if (!fmb_device_monitor_volume_is_visible (volume))
        {
          /* remove from table */
          g_hash_table_steal (monitor->devices, volume);

          /* insert volume in hidden table, take ref from table */
          monitor->hidden_volumes = g_list_prepend (monitor->hidden_volumes, volume);

          /* the device is not visble for the user */
          g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_REMOVED], 0, device);

          /* destroy device */
          g_object_unref (G_OBJECT (device));
        }
      else
        {
          /* the device changed */
          g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_CHANGED], 0, device);
        }
    }
}



static void
fmb_device_monitor_mount_added (GVolumeMonitor      *volume_monitor,
                                   GMount              *mount,
                                   FmbDeviceMonitor *monitor)
{
  FmbDevice     *device;
  GFile            *location;
  FmbDeviceKind  kind = FMB_DEVICE_KIND_MOUNT_LOCAL;
  GVolume          *volume;
  gchar            *id;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_MOUNT (mount));

  /* check if the mount is not already known */
  if (g_hash_table_lookup (monitor->devices, mount) != NULL)
    return;

  /* never handle shadowed mounts */
  if (g_mount_is_shadowed (mount))
    return;

  /* volume for this mount */
  volume = g_mount_get_volume (mount);
  if (volume == NULL)
    {
      location = g_mount_get_root (mount);
      if (G_UNLIKELY (location == NULL))
        return;

      /* skip ghoto locations, since those also have a volume */
      if (g_file_has_uri_scheme (location, "gphoto2"))
        {
          g_object_unref (location);
          return;
        }

      if (g_file_has_uri_scheme (location, "file")
          || g_file_has_uri_scheme (location, "archive"))
        kind = FMB_DEVICE_KIND_MOUNT_LOCAL;
      else
        kind = FMB_DEVICE_KIND_MOUNT_REMOTE;

      g_object_unref (location);

      /* create a new device for this mount */
      device = g_object_new (FMB_TYPE_DEVICE,
                             "device", mount,
                             "kind", kind,
                             NULL);

      /* set visibility */
      id = fmb_device_get_identifier (device);
      g_object_set (G_OBJECT (device),
                    "hidden", fmb_device_monitor_id_is_hidden (monitor, id),
                    NULL);
      g_free (id);

      /* insert to list */
      g_hash_table_insert (monitor->devices, g_object_ref (mount), device);

      /* notify */
      g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_ADDED], 0, device);
    }
  else
    {
      /* maybe we mounted a volume */
      device = g_hash_table_lookup (monitor->devices, volume);
      if (device != NULL)
        {
          /* reload the related FmbFile */
          fmb_device_reload_file (device);

          /* notify */
          g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_CHANGED], 0, device);
        }

      g_object_unref (volume);
    }
}



static void
fmb_device_monitor_mount_removed (GVolumeMonitor      *volume_monitor,
                                     GMount              *mount,
                                     FmbDeviceMonitor *monitor)
{
  FmbDevice *device;
  GVolume      *volume;
  GFile        *root_file;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_MOUNT (mount));

  /* check if we have a device for this mount */
  device = g_hash_table_lookup (monitor->devices, mount);
  if (device != NULL)
    {
      /* notify */
      g_signal_emit (G_OBJECT (monitor),device_monitor_signals[DEVICE_REMOVED], 0, device);

      /* drop it */
      g_hash_table_remove (monitor->devices, mount);
    }
  else
    {
      /* maybe a mount was removed from a known volume, gphoto2 does this */
      volume = g_mount_get_volume (mount);
      if (volume != NULL)
        {
          device = g_hash_table_lookup (monitor->devices, volume);
          if (device != NULL)
            {
              /* we can't get the file from the volume at this point so provide it */
              root_file = g_mount_get_root (mount);
              g_signal_emit (G_OBJECT (monitor),
                             device_monitor_signals[DEVICE_PRE_UNMOUNT], 0,
                             device, root_file);
              g_object_unref (root_file);
            }

          g_object_unref (volume);
        }
    }
}



static void
fmb_device_monitor_mount_changed (GVolumeMonitor      *volume_monitor,
                                     GMount              *mount,
                                     FmbDeviceMonitor *monitor)
{
  FmbDevice *device;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_MOUNT (mount));

  /* check if we have a device for this mount */
  device = g_hash_table_lookup (monitor->devices, mount);
  if (device != NULL)
    {
      /* notify */
      g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_CHANGED], 0, device);
    }
}



static void
fmb_device_monitor_mount_pre_unmount (GVolumeMonitor      *volume_monitor,
                                         GMount              *mount,
                                         FmbDeviceMonitor *monitor)
{
  FmbDevice *device;
  GVolume      *volume;
  GFile        *root_file;

  _fmb_return_if_fail (G_IS_VOLUME_MONITOR (volume_monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (monitor->volume_monitor == volume_monitor);
  _fmb_return_if_fail (G_IS_MOUNT (mount));

  /* check if we have a device for this mount */
  device = g_hash_table_lookup (monitor->devices, mount);
  if (device == NULL)
    {
      /* maybe a volume device? */
      volume = g_mount_get_volume (mount);
      if (volume != NULL)
        {
          device = g_hash_table_lookup (monitor->devices, volume);
          g_object_unref (volume);
        }
    }

  if (device != NULL)
    {
      /* notify */
      root_file = g_mount_get_root (mount);
      g_signal_emit (G_OBJECT (monitor),
                     device_monitor_signals[DEVICE_PRE_UNMOUNT],
                     0, device, root_file);
      g_object_unref (root_file);
    }
}



static void
fmb_device_monitor_list_prepend (gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  GList **list = user_data;

  _fmb_return_if_fail (FMB_IS_DEVICE (value));
  *list = g_list_prepend (*list, g_object_ref (value));
}



FmbDeviceMonitor *
fmb_device_monitor_get (void)
{
static FmbDeviceMonitor *monitor = NULL;

  if (G_UNLIKELY (monitor == NULL))
    {
      monitor = g_object_new (FMB_TYPE_DEVICE_MONITOR, NULL);
      g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer) &monitor);
    }
  else
    {
      g_object_ref (G_OBJECT (monitor));
    }

  return monitor;
}



GList *
fmb_device_monitor_get_devices (FmbDeviceMonitor *monitor)
{
  GList *list = NULL;

  _fmb_return_val_if_fail (FMB_IS_DEVICE_MONITOR (monitor), NULL);

  g_hash_table_foreach (monitor->devices, fmb_device_monitor_list_prepend, &list);

  return list;
}



void
fmb_device_monitor_set_hidden (FmbDeviceMonitor *monitor,
                                  FmbDevice        *device,
                                  gboolean             hidden)
{
  gchar  *id;
  gchar **devices;
  guint   length;
  guint   n;
  guint   pos;

  _fmb_return_if_fail (FMB_IS_DEVICE_MONITOR (monitor));
  _fmb_return_if_fail (FMB_IS_DEVICE (device));

  id = fmb_device_get_identifier (device);
  if (id == NULL)
    return;

  /* update device */
  g_object_set (G_OBJECT (device), "hidden", hidden, NULL);
  g_signal_emit (G_OBJECT (monitor), device_monitor_signals[DEVICE_CHANGED], 0, device);

  /* update the device list */
  length = monitor->hidden_devices != NULL ? g_strv_length (monitor->hidden_devices) : 0;
  devices = g_new0 (gchar *, length + 2);
  pos = 0;

  /* copy other identifiers in the new list */
  if (monitor->hidden_devices != NULL)
    {
      for (n = 0; monitor->hidden_devices[n] != NULL; n++)
        if (g_strcmp0 (monitor->hidden_devices[n], id) != 0)
          devices[pos++] = g_strdup (monitor->hidden_devices[n]);
    }

  /* add the new identifiers if it should hide */
  if (hidden)
    devices[pos++] = id;
  else
    g_free (id);

  /* store new list */
  g_object_set (G_OBJECT (monitor->preferences), "hidden-devices", devices, NULL);
  g_strfreev (devices);
}
