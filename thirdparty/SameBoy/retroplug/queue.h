#ifndef QUEUE_H
#define QUEUE_H


#include <stdio.h>
#include <stdlib.h>

#define MAX_QUEUE_SIZE 100

struct offset_byte_t {
    int offset;
    char byte;
};
typedef struct offset_byte_t offset_byte_t;

struct Queue {
  offset_byte_t data[MAX_QUEUE_SIZE];
  int first;
  int last;
  int size;
};

typedef struct Queue Queue;

static offset_byte_t queue_empty = { -1, -1 };


void queue_init(Queue *);
void enqueue(Queue *, offset_byte_t item);
offset_byte_t  dequeue(Queue *);
int  length(Queue *);
int  empty(Queue *);


void queue_init(Queue *data) {
  data->first = 0;
  data->last  = MAX_QUEUE_SIZE-1;
  data->size  = 0;
}


int empty(Queue *data) {
  return data->size == 0;
}


int length(Queue *data) {
  return data->size;
}


void enqueue(Queue *data, offset_byte_t item) {
  if(data->size < MAX_QUEUE_SIZE) {
    data->last = (data->last + 1) % MAX_QUEUE_SIZE;
    data->data[data->last] = item;
    data->size++;
  }
}

offset_byte_t peek(Queue *data) {
  if (!empty(data)) {
    return data->data[data->first];
  }

  return queue_empty;
}

offset_byte_t dequeue(Queue *data) {
  if(!empty(data)) {
    offset_byte_t last = data->data[data->first];

    data->first = (data->first + 1) % MAX_QUEUE_SIZE;
    data->size--;

    return last;
  }

  return queue_empty;
}

#endif
