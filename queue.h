#ifndef LIST_H
#define LIST_H

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef struct List *List;
typedef struct Node *Node;

List queueConstructor();

int queueDestructor(List list);

int appendNewRequest(List list, int value1, struct timeval arrivalTime);

int append(List list, Node node, int threadId);

int getSize(List list);

int removeByValue(List list, int value1);

Node removeFront(List list);

int removeByIndex(List list, int index);

int getValue(Node node);

int getHandlerThread_id(Node node);

struct timeval getArrivalTime(Node node);

struct timeval getDispatchTime(Node node);


#endif // LIST_H
