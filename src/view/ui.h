#ifndef VIEW_UI_H
#define VIEW_UI_H

#include <stdbool.h>

typedef struct {
    int selected;
    int hovered;
    bool follow_log;
} Ui;

bool ui_init(int width, int height);
void ui_close(void);
void ui_update(Ui *ui, int width, int height);
void ui_draw(const Ui *ui);

#endif
