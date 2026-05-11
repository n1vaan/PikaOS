#ifndef UI_PET_H
#define UI_PET_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Pet_screen_init(void);
extern void ui_Pet_screen_destroy(void);
extern void ui_event_Pet(lv_event_t * e);
extern lv_obj_t *ui_Pet;

#ifdef __cplusplus
}
#endif

#endif
