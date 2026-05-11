// Pet screen — yellow background. Click anywhere plays a random pikachu sound
// (the click handler lives in os.ino so it can touch play_chime / chime_clip_idx).

#include "ui.h"

lv_obj_t *ui_Pet = NULL;

void ui_event_Pet(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT) {
            lv_indev_wait_release(lv_indev_get_act());
            _ui_screen_change(&ui_PokeBallAnalog, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_PokeBallAnalog_screen_init);
        } else if (dir == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_get_act());
            _ui_screen_change(&ui_Photo2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_Photo2_screen_init);
        } else if (dir == LV_DIR_TOP) {
            lv_indev_wait_release(lv_indev_get_act());
            _ui_screen_change(&ui_Settings, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, &ui_Settings_screen_init);
        }
    }
}

void ui_Pet_screen_init(void) {
    ui_Pet = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Pet, LV_OBJ_FLAG_SCROLLABLE);
    ui_object_set_themeable_style_property(ui_Pet, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_Default_Yellow);
    ui_object_set_themeable_style_property(ui_Pet, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_Default_Yellow);
    lv_obj_add_flag(ui_Pet, LV_OBJ_FLAG_CLICKABLE); // so taps register
    lv_obj_add_event_cb(ui_Pet, ui_event_Pet, LV_EVENT_ALL, NULL);
}

void ui_Pet_screen_destroy(void) {
    if (ui_Pet) lv_obj_del(ui_Pet);
    ui_Pet = NULL;
}
