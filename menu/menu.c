/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <file/file_path.h>

#include "menu.h"
#include "menu_cbs.h"
#include "menu_hash.h"
#include "menu_shader.h"

#include "../general.h"
#include "../frontend/frontend.h"

static void menu_environment_get(int *argc, char *argv[],
      void *args, void *params_data)
{
   struct rarch_main_wrap *wrap_args = (struct rarch_main_wrap*)params_data;
   global_t *global     = global_get_ptr();
   settings_t *settings = config_get_ptr();
   menu_handle_t *menu  = menu_driver_get_ptr();
    
   if (!wrap_args)
      return;

   wrap_args->no_content       = menu->load_no_content;
   if (!global->has_set.verbosity)
      wrap_args->verbose       =  global->verbosity;

   wrap_args->config_path      = *global->path.config   ? global->path.config   : NULL;
   wrap_args->sram_path        = *global->dir.savefile  ? global->dir.savefile  : NULL;
   wrap_args->state_path       = *global->dir.savestate ? global->dir.savestate : NULL;
   wrap_args->content_path     = *global->path.fullpath ? global->path.fullpath : NULL;

   if (!global->has_set.libretro)
      wrap_args->libretro_path = *settings->libretro ? settings->libretro : NULL;
   wrap_args->touched       = true;
}

static void menu_push_to_history_playlist(void)
{
   settings_t *settings = config_get_ptr();
   global_t *global     = global_get_ptr();

   if (!settings->history_list_enable)
      return;

   if (*global->path.fullpath)
   {
      char tmp[PATH_MAX_LENGTH];
      char str[PATH_MAX_LENGTH];

      fill_pathname_base(tmp, global->path.fullpath, sizeof(tmp));
      snprintf(str, sizeof(str), "INFO - Loading %s ...", tmp);
      menu_display_msg_queue_push(str, 1, 1, false);
   }

   content_playlist_push(g_defaults.history,
         global->path.fullpath,
         NULL,
         settings->libretro,
         global->menu.info.library_name,
         NULL,
         NULL);
}

/**
 * menu_load_content:
 *
 * Loads content into currently selected core.
 * Will also optionally push the content entry to the history playlist.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
bool menu_load_content(enum rarch_core_type type)
{
   menu_handle_t *menu  = menu_driver_get_ptr();
   menu_display_t *disp = menu_display_get_ptr();
   driver_t *driver     = driver_get_ptr();
   global_t *global     = global_get_ptr();

   /* redraw menu frame */
   if (disp)
      disp->msg_force = true;

   menu_iterate(true, MENU_ACTION_NOOP);
   menu_iterate_render();

   if (!(main_load_content(0, NULL, NULL, menu_environment_get,
         driver->frontend_ctx->process_args)))
   {
      char name[PATH_MAX_LENGTH] = {0};
      char msg[PATH_MAX_LENGTH]  = {0};

      fill_pathname_base(name, global->path.fullpath, sizeof(name));
      snprintf(msg, sizeof(msg), "Failed to load %s.\n", name);
      menu_display_msg_queue_push(msg, 1, 90, false);

      if (disp)
         disp->msg_force = true;

      return false;
   }

   menu_shader_manager_init(menu);

   event_command(EVENT_CMD_HISTORY_INIT);

   if (*global->path.fullpath || (menu && menu->load_no_content))
      menu_push_to_history_playlist();

   event_command(EVENT_CMD_VIDEO_SET_ASPECT_RATIO);
   event_command(EVENT_CMD_RESUME);

   return true;
}

int menu_common_load_content(
      const char *core_path, const char *fullpath,
      bool persist, enum rarch_core_type type)
{
   settings_t *settings         = config_get_ptr();
   global_t *global             = global_get_ptr();
   menu_display_t *disp         = menu_display_get_ptr();
   menu_list_t *menu_list       = menu_list_get_ptr();
   enum event_command cmd       = EVENT_CMD_NONE;

   if (core_path)
   {
      strlcpy(settings->libretro, core_path, sizeof(settings->libretro));
      event_command(EVENT_CMD_LOAD_CORE);
   }

   if (fullpath)
   {
      strlcpy(global->path.fullpath, fullpath, sizeof(global->path.fullpath));
   }

   switch (type)
   {
      case CORE_TYPE_PLAIN:
      case CORE_TYPE_DUMMY:
         cmd = persist ? EVENT_CMD_LOAD_CONTENT_PERSIST : EVENT_CMD_LOAD_CONTENT;
         break;
#ifdef HAVE_FFMPEG
      case CORE_TYPE_FFMPEG:
         cmd = EVENT_CMD_LOAD_CONTENT_FFMPEG;
         break;
#endif
      case CORE_TYPE_IMAGEVIEWER:
#ifdef HAVE_IMAGEVIEWER
         cmd = EVENT_CMD_LOAD_CONTENT_IMAGEVIEWER;
#endif
         break;
   }

   if (cmd != EVENT_CMD_NONE)
      event_command(cmd);

   menu_list_flush_stack(menu_list, NULL, MENU_SETTINGS);
   disp->msg_force = true;

   generic_action_ok_displaylist_push("",
         "", 0, 0, 0, ACTION_OK_DL_CONTENT_SETTINGS);

   return -1;
}

/**
 * menu_free:
 * @menu                     : Menu handle.
 *
 * Frees a menu handle
 **/
void menu_free(menu_handle_t *menu)
{
   global_t        *global    = global_get_ptr();
   menu_display_t    *disp    = menu_display_get_ptr();
   if (!menu || !disp)
      return;

   if (menu->playlist)
      content_playlist_free(menu->playlist);
   menu->playlist = NULL;
  
   menu_shader_free(menu);

   menu_driver_free(menu);

#ifdef HAVE_DYNAMIC
   libretro_free_system_info(&global->menu.info);
#endif

   menu_display_free(menu);

   menu_entries_free();

   event_command(EVENT_CMD_HISTORY_DEINIT);

   if (global->core_info.list)
      core_info_list_free(global->core_info.list);

   if (global->core_info.current)
      free(global->core_info.current);
   global->core_info.current = NULL;

   free(menu);
}

/**
 * menu_init:
 * @data                     : Menu context handle.
 *
 * Create and initialize menu handle.
 *
 * Returns: menu handle on success, otherwise NULL.
 **/
void *menu_init(const void *data)
{
   menu_handle_t *menu         = NULL;
   menu_display_t *disp        = NULL;
   menu_ctx_driver_t *menu_ctx = (menu_ctx_driver_t*)data;
   global_t  *global           = global_get_ptr();
   settings_t *settings        = config_get_ptr();
   
   if (!menu_ctx)
      return NULL;

   if (!(menu = (menu_handle_t*)menu_ctx->init()))
      return NULL;

   strlcpy(settings->menu.driver, menu_ctx->ident,
         sizeof(settings->menu.driver));

   if (!menu_entries_init(menu))
      goto error;

   global->core_info.current = (core_info_t*)calloc(1, sizeof(core_info_t));
   if (!global->core_info.current)
      goto error;

#ifdef HAVE_SHADER_MANAGER
   menu->shader = (struct video_shader*)calloc(1, sizeof(struct video_shader));
   if (!menu->shader)
      goto error;
#endif

   menu->push_help_screen           = settings->menu_show_start_screen;
   menu->help_screen_type           = MENU_HELP_WELCOME;
   settings->menu_show_start_screen = false;

#if 0
   if (settings->bundle_assets_extract_enable &&
         (strcmp(PACKAGE_VERSION, settings->bundle_assets_last_extracted_version) != 0)
      )
   {
      menu->push_help_screen = true;
      menu->help_screen_type = MENU_HELP_EXTRACT;

      rarch_main_data_msg_queue_push(DATA_TYPE_FILE, "cb_bundle_extract", "cb_bundle_extract", 0, 1, true);
   }
#endif

   menu_shader_manager_init(menu);

   if (!menu_display_init(menu))
      goto error;

   disp = &menu->display;

   rarch_assert(disp->msg_queue = msg_queue_new(8));

   return menu;
   
error:
   menu_free(menu);

   return NULL;
}

