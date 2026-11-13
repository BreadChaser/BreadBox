/* One main-thread producer, one file-writer consumer. No core/UI calls on worker. */
#ifndef SAVE_WRITER_H
#define SAVE_WRITER_H
#include <stdbool.h>
#include <stddef.h>
typedef struct save_writer save_writer;
typedef bool (*save_write_fn)(const void *snapshot);
save_writer *save_writer_start(size_t bytes, save_write_fn write, int core);
/* Copies bytes before returning; replaces only the not-yet-started snapshot. */
void save_writer_submit(save_writer *, const void *snapshot);
/* Drains active AND pending writes. Last requested snapshot must be durable. */
bool save_writer_flush(save_writer *);
bool save_writer_failed(save_writer *);
void save_writer_stop(save_writer *);
#endif
