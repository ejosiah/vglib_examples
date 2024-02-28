#ifndef DATA_STRUCTURE_GLSL
#define DATA_STRUCTURE_GLSL

#define MAX_STACK_CAPACITY 30   // 29 is the maximum stack size I've seen from testing
#define MAX_SET_SIZE 20 // less than 20 active visited nodes I've seen from testing
#ifndef inout
#define inout
#endif // inout

void swap(inout int& a, inout int& b) {
    int temp = a;
    a = b;
    b = temp;
}

struct Stack {
    int data[MAX_STACK_CAPACITY];
    int size;
};

Stack createStack() {
    Stack stack;
    stack.size = 0;
    return stack;
}

void push(inout Stack& stack, int entry) {
    stack.data[stack.size++] = entry;
}

int top(inout Stack& stack) {
    return stack.data[stack.size - 1];
}

int pop(inout Stack& stack) {
    return stack.data[--stack.size];
}

bool empty(Stack& stack) {
    return stack.size == 0;
}

struct Set {
    int data[MAX_SET_SIZE];
    int size;
};

Set createSet() {
    Set set;
    set.size = 0;
    return set;
}

void insert(inout Set& set, int entry) {
    set.data[set.size++] = entry;
}

int findPosition(Set& set, int entry) {
    int pos = -1;   // small set so brute force search is ok
    for(int i = 0; i < set.size; i++){
        if(set.data[i] == entry) {
            pos = i;
            break;
        }
    }
    return pos;
}

bool remove(inout Set& set, int entry){
    int pos = findPosition(set, entry);
    if(pos != -1) {
        int last = set.size - 1;
        if(pos != last) {
            swap(set.data[pos], set.data[last]);
        }
        set.size--;
    }
    return pos != -1;
}

bool contains(inout Set& set, int entry) {
    return findPosition(set, entry) != -1;
}

#endif // DATA_STRUCTURE_GLSL