#include "queue.h"

struct List {
    int size;
    struct Node *head;
    struct Node *tail;
};
struct Node {
    int value;
    int handlerThread;
    struct timeval arrival_time;
    struct timeval dispatch_time;
    struct Node *next;
};

List queueConstructor() {
    List list = (List) malloc(sizeof(*list));
    if (list == NULL) {
        return NULL;
    }
    list->size = 0;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

Node nodeConstructor(int value1, struct timeval arrivalTime) {
    Node node = (Node) malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->value = value1;
    node->arrival_time = arrivalTime;
    node->next = NULL;
    return node;
}

int queueDestructor(List list) {
    if (list == NULL) {
        return -1;
    }
    Node node = list->head;
    while (node != NULL) {
        Node delete = node;
        node = node->next;
        delete->next = NULL;
        free(delete);
    }
    free(list);
    return 1;
}

int appendNewRequest(List list, int value1, struct timeval arrivalTime) {
    if (list == NULL) {
        return -1;
    }
    Node to_add = nodeConstructor(value1, arrivalTime);
    if (to_add == NULL) {
        return -1;
    }
    if (list->size == 0) {
        list->head = to_add;
        list->tail = to_add;
        list->size++;
    } else {
        list->tail->next = to_add;
        list->tail = to_add;
        list->size++;
    }

    return 1;
}

int append(List list, Node node, int threadId) {
    if (list == NULL) {
        return -1;
    }
    if (node == NULL) {
        return -1;
    }
    struct timeval time;
    gettimeofday(&time, NULL);
    if (list->size == 0) {
        list->head = node;
        list->tail = node;
        timersub(&time, &node->arrival_time, &node->dispatch_time);
        node->handlerThread = threadId;
        list->size++;
    } else {
        list->tail->next = node;
        list->tail = node;
        timersub(&time, &node->arrival_time, &node->dispatch_time);
        node->handlerThread = threadId;
        list->size++;
    }
    return 1;
}

Node removeFront(List list) {
    if (list == NULL) {
        return NULL;
    }
    if (list->size == 0) {
        return NULL;
    }
    if (list->size == 1) {
        Node toDelete = list->head;
        list->head = NULL;
        list->tail = NULL;
        list->size--;
        toDelete->next = NULL;
        return toDelete;
    } else {
        Node toDelete = list->head;
        Node next_head = toDelete->next;
        list->head = next_head;
        list->size--;
        toDelete->next = NULL;
        return toDelete;

    }
}

int getSize(List list) {
    return list->size;
}

int removeByValue(List list, int value1) {
    if (list == NULL) {
        return 0;
    }
    Node temp = list->head;
    if (temp->value == value1) {
        if (list->size == 1) {
            list->head = NULL;
            list->tail = NULL;
            int toReturn = temp->value;
            free(temp);
            list->size--;
            return toReturn;
        }
        list->head = temp->next;
        int toReturn = temp->value;
        free(temp);
        list->size--;
        return toReturn;
    } // not in the beginning
    while (temp != NULL) {
        if (temp->value == value1) {
            break;
        }
        temp = temp->next;
    }
    if (temp == NULL) {
        return -1;
    }
    Node delete = list->head;
    while (delete->next != temp) {
        delete = delete->next;
    }
    if (temp->next == NULL) {//temp is in tail
        delete->next = NULL;
        list->tail = delete;
        int toReturn = temp->value;
        list->size--;
        free(temp);
        return toReturn;
    }
    delete->next = delete->next->next;
    temp->next = NULL;
    int toReturn = temp->value;
    list->size--;
    free(temp);
    return toReturn;
}

int removeByIndex(List list, int index) {
    if (index >= list->size) {
        return -1;
    }
    int i = 0;
    Node temp = list->head;
    while (i != index) {
        if (temp == NULL) {
            return -1;
        }
        temp = temp->next;
        i++;
    }
    int valueToRemove = temp->value;
    removeByValue(list, valueToRemove);
    return valueToRemove;
}

int getValue(Node node) {
    return node->value;
}

int getHandlerThread_id(Node node) {
    return node->handlerThread;
}

struct timeval getArrivalTime(Node node) {
    return node->arrival_time;
}

struct timeval getDispatchTime(Node node) {
    return node->dispatch_time;
}


