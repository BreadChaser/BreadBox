#pragma once
/* Shared value-card paging: reserve a cue line only when the text overflows. */
static inline int ui_text_page_count(int lines,int rows,int cue)
{
 if(lines<=rows)return 1;
 int capacity=rows-cue;
 return (lines+capacity-1)/capacity;
}
