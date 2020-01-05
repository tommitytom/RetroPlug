#ifndef QUEUE_H
#define QUEUE_H


#include <stdio.h>
#include <stdlib.h>

struct Queue {
  char* data;
  int first;
  int last;
  int size;
  int maxSize;
  int elemSize;
};

typedef struct Queue Queue;

void queue_init(Queue* queue, char* data, size_t elemSize, size_t elemCount);
void queue_enqueue(Queue* queue, void* item);
void* queue_dequeue(Queue* queue);
int queue_length(Queue* queue);
int queue_empty(Queue* queue);
int queue_full(Queue* queue);
int queue_maxLength(Queue* queue);

void queue_init(Queue* queue, char* data, size_t elemSize, size_t elemCount) {
  queue->elemSize = elemSize;
  queue->maxSize = elemCount;
  queue->data = data;
  queue->first = 0;
  queue->last = elemCount - 1;
  queue->size = 0;
}

int queue_empty(Queue* queue) {
  return queue->size == 0;
}

int queue_maxLength(Queue* queue) {
  return queue->maxSize;
}

int queue_full(Queue* queue) {
  return queue->size == queue->maxSize;
}

int queue_length(Queue* queue) {
  return queue->size;
}

void queue_enqueue(Queue* queue, void* item) {
  if (queue->size < queue->maxSize) {
    queue->last = (queue->last + 1) % queue->maxSize;
    memcpy(queue->data + (queue->last * queue->elemSize), item, queue->elemSize);
    queue->size++;
  }
}

void* queue_front(Queue* queue) {
  if (!queue_empty(queue)) {
    return queue->data + (queue->first * queue->elemSize);
  }

  return NULL;
}

void* queue_back(Queue* queue) {
  if (!queue_empty(queue)) {
    return queue->data + (queue->last * queue->elemSize);
  }

  return NULL;
}

void* queue_get(Queue* queue, int idx) {
  if (idx < queue->size) {
    int elemIdx = (queue->first + idx) % queue->maxSize;
    return queue->data + (elemIdx * queue->elemSize);
  }

  return NULL;
}

void* queue_dequeue(Queue* queue) {
  if(!queue_empty(queue)) {
    void* last = queue->data + (queue->first * queue->elemSize);
    queue->first = (queue->first + 1) % queue->maxSize;
    queue->size--;
    return last;
  }

  return NULL;
}

#endif
