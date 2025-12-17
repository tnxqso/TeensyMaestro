/*
  Library support for FlexRadio 6000 Rigs
  Copyright (C)2015 Vincenzo Stefanazzi - IW7DMH. All right reserved

  This library is free software; you can redistribute it and/or
  modify it under the terms of the CC BY-NC-SA 3.0 license.
  https://creativecommons.org/licenses/by-nc-sa/3.0/
  
  The license applies to all part of the library including the 
  examples and tools supplied with the library.
*/
#ifndef Spots_h
#define Spots_h
#include "Arduino.h"

extern "C" {
	typedef void (*eventHandlerArrFunction)(const int senderId);
}

class Spots
{
  public:
    //properties

	int spot_triggered; 
	
//	int previousBand;

    //methods
    Spots();
	void updateStatus(String msg);
	void updateObject(int objectId,int value);  //Update specific panadapter object	

	void set_spot_triggered(int newval);
		
	//event handler accessor methods

	void attach_spot_triggered_event(eventHandlerArrFunction _eventHandler);
	
	//
	void fireEvents();
	boolean updated;
  private:
    //properties
	
	//Update flags

	boolean upd_spot_triggered; 

	//event handlers

	eventHandlerArrFunction do_spot_triggered_event=NULL;
	
    //methods	
	void setValue(String msg);
};
	
#endif