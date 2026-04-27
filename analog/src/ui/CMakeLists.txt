SET(SOURCES ui_comp_container1.c
    ui_comp.c
    ui_PokeBallAnalog.c
    ui_Screen1.c
    ui_Analog.c
    ui_Timer.c
    ui_Settings.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_835555188.c
    ui_img_913658961.c
    fonts/ui_font_Font1.c
    fonts/ui_font_Pokemon.c)

add_library(ui ${SOURCES})
