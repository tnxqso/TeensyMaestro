/*
  Library support for FlexRadio 6000 Rigs
  Copyright (C)2015 Vincenzo Stefanazzi - IW7DMH. All right reserved

  This library is free software; you can redistribute it and/or
  modify it under the terms of the CC BY-NC-SA 3.0 license.
  https://creativecommons.org/licenses/by-nc-sa/3.0/
  
  The license applies to all part of the library including the 
  examples and tools supplied with the library.
*/
#include "FlexRigSilentSerial.h"

#include "CommandList.h"

// Place the heavy String array in OCRAM (RAM2)
DMAMEM String CommandList::CommandEntry[COMMAND_LIST_SIZE];

CommandList::CommandList() {
  front = back = -1;
  count = 0;

  // Reserve capacity in RAM2 to minimize realloc/moves during add()
  // This allocerar i RAM2 på T4.1, påverkar inte RAM1.
  for (int i = 0; i < COMMAND_LIST_SIZE; ++i) {
    CommandEntry[i].reserve(COMMAND_LEGTH);
    CommandEntry[i] = "";
  }
}

void CommandList::add(String entry) {
  if ((front == 0 && back == COMMAND_LIST_SIZE - 1) || front == back + 1) {
    Serial.println(F("Command List full!"));
  } else if (front == -1 && back == -1) {
    front = back = 0;
    CommandEntry[front] = entry;
    count++;
  } else if (back == COMMAND_LIST_SIZE - 1) {
    back = 0;
    CommandEntry[back] = entry;
    count++;
  } else {
    back++;
    CommandEntry[back] = entry;
    count++;
  }
}

String CommandList::remove() {
  String element;
  if (front == -1 && back == -1) {
    Serial.println(F("Command List is empty"));
  } else {
    element = CommandEntry[front];
    if (front == back) {
      front = back = -1;
      count--;
    } else if (front == COMMAND_LIST_SIZE - 1) {
      front = 0;
      count--;
    } else {
      front++;
      count--;
    }
  }
  return element;
}

int CommandList::getCount() {
  return count;
}