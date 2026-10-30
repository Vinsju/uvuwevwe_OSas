#include <stdint.h>
#include <stdbool.h>
#include "header/text/terminal.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"

#define TERMINAL_ROW_BYTES (TERMINAL_WIDTH * 2)
#define TERMINAL_DEFAULT_COLOR 0x07

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

/* Geser seluruh isi layar 1 baris ke atas, baris terakhir dikosongkan */
static void terminal_scroll(void)
{
    uint8_t *fb = FRAMEBUFFER_MEMORY_OFFSET;
    memmove(fb, fb + TERMINAL_ROW_BYTES, TERMINAL_ROW_BYTES * (TERMINAL_HEIGHT - 1));
    for (uint8_t col = 0; col < TERMINAL_WIDTH; col++)
    {
        framebuffer_write(TERMINAL_HEIGHT - 1, col, ' ', TERMINAL_DEFAULT_COLOR, 0);
    }
}

static void terminal_newline(void)
{
    cursor_col = 0;
    if (cursor_row + 1 >= TERMINAL_HEIGHT)
    {
        terminal_scroll();
        cursor_row = TERMINAL_HEIGHT - 1;
    }
    else
    {
        cursor_row++;
    }
}

static void terminal_put_raw(char c, uint8_t color)
{
    framebuffer_write(cursor_row, cursor_col, c, color & 0x0F, (color >> 4) & 0x0F);
    cursor_col++;
    if (cursor_col >= TERMINAL_WIDTH)
    {
        terminal_newline();
    }
}

void terminal_putchar(char c, uint8_t color)
{
    switch (c)
    {
    case '\n':
        terminal_newline();
        break;
    case '\r':
        cursor_col = 0;
        break;
    case '\b':
        if (cursor_col > 0)
        {
            cursor_col--;
        }
        else if (cursor_row > 0)
        {
            cursor_row--;
            cursor_col = TERMINAL_WIDTH - 1;
        }
        framebuffer_write(cursor_row, cursor_col, ' ', TERMINAL_DEFAULT_COLOR, 0);
        break;
    case '\t':
        do
        {
            terminal_put_raw(' ', color);
        } while (cursor_col % 4 != 0);
        break;
    default:
        terminal_put_raw(c, color);
        break;
    }
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void terminal_puts(const char *str, uint32_t count, uint8_t color)
{
    for (uint32_t i = 0; i < count; i++)
    {
        terminal_putchar(str[i], color);
    }
}

void terminal_clear(void)
{
    framebuffer_clear();
    cursor_row = 0;
    cursor_col = 0;
    framebuffer_set_cursor(0, 0);
}

void terminal_set_cursor(uint8_t row, uint8_t col)
{
    cursor_row = row < TERMINAL_HEIGHT ? row : TERMINAL_HEIGHT - 1;
    cursor_col = col < TERMINAL_WIDTH ? col : TERMINAL_WIDTH - 1;
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void terminal_get_cursor(uint8_t *row, uint8_t *col)
{
    *row = cursor_row;
    *col = cursor_col;
}
