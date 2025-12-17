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
#include "Spots.h"

Spots::Spots() {

	upd_spot_triggered=true; 	
	updated=false;
	//previousBand=-1;
}


FLASHMEM void Spots::updateStatus(String msg) {
	
	//Serial.println(msg);
	updated=true;
	int oldi=0;
	int i=msg.indexOf(' ',oldi);
	while (i>0) {
		setValue(msg.substring(oldi,i));
		oldi=i+1;
		i=msg.indexOf(' ',oldi+1);
	}	
	//get the last element
	i=msg.indexOf('\n',oldi);
	setValue(msg.substring(oldi,i));
	
}

FLASHMEM void Spots::setValue(String msg){
	int i=msg.indexOf('=');
	if (i>0) {
	    String var=msg.substring(0,i);
		//Serial.println(var);
		
//		if (var.equals(F("pan"))) {
//		   	set_pan(msg.substring(i+3).toInt()); 
//			return;
//		} 

		if (var.equals(F("triggered"))) {
			Serial.print("*************** Spot: ");
			Serial.println(msg.substring(i+1));
			set_spot_triggered(msg.substring(i+1).toInt()); 
			return; 
		} 

		
	}
}

//FLASHMEM void Spots::set_pan(int newval) {
//    if (newval!=pan) {
//		pan=newval;
//		upd_pan=true;
//	}
//}


FLASHMEM void Spots::set_spot_triggered(int newval) {
	if (newval!=spot_triggered) {
		spot_triggered=newval;
		upd_spot_triggered=true;
		Serial.print("Spot triggered: ");
		Serial.println(spot_triggered);
	}
}

FLASHMEM void Spots::fireEvents() {

	


	if ((upd_spot_triggered) && (do_spot_triggered_event!=NULL)) {
			do_spot_triggered_event(1073741824); 
			upd_spot_triggered=false; 
	}

}



FLASHMEM void Spots::attach_spot_triggered_event(eventHandlerArrFunction _eventHandler) {
	do_spot_triggered_event=_eventHandler;
}

