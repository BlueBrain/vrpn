/*
 *
 * vrpn_tracker_GPS  added to allow GPS tracking in outdoor AR experiences
 *  
 */

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <mbstring.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#ifdef linux
#include <termios.h>
#endif

#ifndef _WIN32
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#endif

#include "vrpn_Tracker.h"
#include "vrpn_Tracker_GPS.h"
#include "vrpn_Serial.h"
#include "vrpn_Shared.h"

#define MAX_TIME_INTERVAL       (5000000) // max time between reports (usec)
#define	INCHES_TO_METERS	(2.54/100.0)
#define PI (3.14159265358979323846)
#define	FT_INFO(msg)	{ send_text_message(msg, timestamp, vrpn_TEXT_NORMAL) ; if (d_connection) d_connection->send_pending_reports(); }
#define	FT_WARNING(msg)	{ send_text_message(msg, timestamp, vrpn_TEXT_WARNING) ; if (d_connection) d_connection->send_pending_reports(); }
#define	FT_ERROR(msg)	{ send_text_message(msg, timestamp, vrpn_TEXT_ERROR) ; if (d_connection) d_connection->send_pending_reports(); }



static	unsigned long	duration(struct timeval t1, struct timeval t2)
{
	return (t1.tv_usec - t2.tv_usec) +
    1000000L * (t1.tv_sec - t2.tv_sec);
}
//--------------------------------------------------




vrpn_Tracker_GPS::vrpn_Tracker_GPS(const char *name, 
                                   vrpn_Connection *c, 
		                           const char *port, 
                                   long baud,
                                   int utmFlag,
                                   int testFileFlag,
                                   const char* startSentence) :
vrpn_Tracker_Serial(name,c,port,baud)
{
    //this sets the name of the testfile
    sprintf(testfilename, "GPS-data.txt"); 				
    
    // This allow people to set an external flag whether they want to use real GPS or not
    //Assuming this is 0 by default
    if (testFileFlag == 1) {
        //MessageBox(NULL, "CFG file flag set to use sample GPS data","FYI",0);
        testfile = fopen(testfilename,"r"); //comment this line out to get real data
    } else {
        //MessageBox(NULL, temp,"Live GPS data",0);
        testfile = NULL; 
    }
    useUTM = utmFlag;
    if (strlen(startSentence) > 0) {
        nmeaParser.setStartSentence((char*)startSentence);
    }
	
  // Set the hardware flow-control on the serial line in case
  // the GPS unit requires it (some do).
  if (serial_fd >= 0) {
	vrpn_set_rts(serial_fd);
	FT_WARNING("Set RTS.\n");
  }
	
    register_server_handlers(); //-eb
	
}

//------------------------------------
vrpn_Tracker_GPS::~vrpn_Tracker_GPS()
{
    //Cleanup
    if (testfile != NULL) fclose(testfile); 
    
    if (serial_fd >=0) {
        vrpn_close_commport(serial_fd);
        serial_fd = -1;
    }
}

//---------------------------------
void vrpn_Tracker_GPS::reset()
{

 if (serial_fd >= 0) {
       vrpn_set_rts(serial_fd);
       		FT_WARNING("Set RTS during reset.\n");
 }
	//printf("serial fd: %d\n",serial_fd);
	if (serial_fd >= 0)
	{
		FT_WARNING("Reset Completed (this is good).\n");
        //we need to give the port a chance to read new bits
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
		status = vrpn_TRACKER_SYNCING;	// We're trying for a new reading
	    
		if (testfile != NULL) fseek(testfile,0L,SEEK_SET);
	} else {
		printf("Serial not detected on reset.  Try power cycle.\n"); //doesn't really ever seem to fail here unless com port gets jammed
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif //prevent the console from spamming us
		status = vrpn_TRACKER_FAIL;
	}
    
	nmeaParser.reset();
}

//-----------------------------------------------------------------
// This function will read characters until it has a full report, then
// put that report into the time, sensor, pos and quat fields so that it can
// be sent the next time through the loop.
int vrpn_Tracker_GPS::get_report(void)
{
	//printf("getting report\n");
	char errmsg[512];	// Error message to send to VRPN
	//int ret;		// Return value from function call to be checked
	int ret;
    
	int i = 0;
    //	unsigned char *bufptr;	// Points into buffer at the current value to read
	int done=0;
    
	//char speed[256];
	//char course[256];
	//	int buflen = 0;
	unsigned int numparameter=0;
	unsigned int index=0;
	int charRead = 0;
    //	char temp [256];
    
	//--------------------------------------------------------------------
	// Each report starts with an ASCII '$' character. If we're synching,
	// read a byte at a time until we find a '$' character.
	//--------------------------------------------------------------------
    
	//printf("STATUS =%d\n", status);
	if(status == vrpn_TRACKER_SYNCING)
	{
		
		// If testfile is live; read data from that
		if (testfile != NULL) { 
            
			//Read one char to see if test file is active
			if(fread(buffer,sizeof(char),1,testfile) != 1)  
			{
				return 0;
			}
            
         //   Else read from the serial port 
		} else {
			
            charRead = vrpn_read_available_characters(serial_fd, buffer, 1);
            
            
			if( charRead == -1)
			{			
				printf("fail to read first bit\n");
				return 0;
			} else if (charRead == 0) {
				//case of successful read but no new bits, take a quick nap, wait for new info.
				
#ifdef _WIN32
                Sleep(10);
#else
                usleep(10000);
#endif
				return 0;
			}
        }
			//if it returns 0 (very likely - happens on interrupt or no new bits) then we have to prevent it from reading the same buffer over and over.
            
            // If it's not = to $, keep going until the beginning of a packet starts
            if( buffer[0] != '$') 
            {
                sprintf(errmsg,"While syncing (looking for '$', got '%c')", buffer[0]);
                
                
                FT_INFO(errmsg);
                vrpn_flush_input_buffer(serial_fd);
                
                return 0;
            }
            
            //printf("We have a $, setting to partial and moving on.\n");
            bufcount = 1;  // external GPS parser lib expects "$" at start
            
            gettimeofday(&timestamp, NULL);
            
            status = vrpn_TRACKER_PARTIAL;
        }//if syncing
        
        while (!done)
        {
            if (testfile != NULL) {
                ret = fread(&buffer[bufcount],sizeof(char),1,testfile); 
            } else {
                ret = vrpn_read_available_characters(serial_fd, &buffer[bufcount], 1);
            }
            
            if (ret == -1) {
                FT_ERROR("Error reading report");
                status = vrpn_TRACKER_FAIL;
                return 0;
            } else if (ret == 0) {
                return 0;
            }
            
            bufcount += ret;
            if (bufcount >= VRPN_TRACKER_BUF_SIZE*10)
            {
                status = vrpn_TRACKER_SYNCING;
                return 0;
            }
            if(buffer[bufcount-1] == '\n')
            {
                buffer[bufcount-1] = '\0';
                done = 1;
            }
        }
        if (nmeaParser.parseSentence((char*)buffer) == SENTENCE_VALID) 
        {
            nmeaData = nmeaParser.getData();
            //printf("Raw lastfixQuality: %d anyValid: %d LAT: %f LONG: %f ALT: %f\n", nmeaData.lastFixQuality, nmeaData.hasCoordEverBeenValid, nmeaData.lat, nmeaData.lon, nmeaData.altitude);
            //required RMC doesn't do altitude, so this will be needlessly false half of the time.
            //if (nmeaData.isValidLat && nmeaData.isValidLon && nmeaData.isValidAltitude) 
            if (nmeaData.isValidLat && nmeaData.isValidLon)
            { 
                if(nmeaData.isValidAltitude){
                
                if (useUTM) 
                {
                    utmCoord.setLatLonCoord (nmeaData.lat, nmeaData.lon);
                    if (!utmCoord.isOutsideUTMGrid ())
                    {
                        double x, y;
                        utmCoord.getXYCoord (x,y);
                        // Christopher 07/25/04: We flip to be x = East <-> West and y = North <-> South
                        //Should this be changed to match rtk precision vrpn_float64?
                        pos[0] = (float)(y);
                        pos[1] = (float)(x);
                        pos[2] = (float)(nmeaData.altitude);
                    }
                } else {
                    
                    pos[0] = (vrpn_float64)(nmeaData.lat);
                    pos[1] = (vrpn_float64)(nmeaData.lon);
                    pos[2] = (vrpn_float64)(nmeaData.altitude);
                }//use utm d           
                /*
                 vel[0] = vel_data[0];
                 vel[1] = vel_data[1];
                 vel[2] = vel_data[2];
                 */
//#ifdef VERBOSE	
                printf("GPS pos: %f, %f, %f\n",pos[0],pos[1],pos[2]);
//#endif
                nmeaParser.reset();
                
                //send report -eb
                //-----------------------------
                //printf("tracker report ready\n",status);
                
                //gettimeofday(&timestamp, NULL);	// Set watchdog now
                
                /*
                 // Send the message on the connection
                 if (NULL != vrpn_Tracker::d_connection) 
                 {
                 char	msgbuf[1000];
                 
                 
                 fprintf(stderr, "position id = %d, sender id = %d", position_m_id, d_sender_id); 
                 //MessageBox(NULL, temp,"GPS Testing",0);
                 
                 // Pack position report
                 int	len = encode_to(msgbuf);
                 if (d_connection->pack_message(len, timestamp,
                 position_m_id, d_sender_id, msgbuf,
                 vrpn_CONNECTION_LOW_LATENCY)) 
                 {
                 
                 fprintf(stderr,"GPS: cannot write message: tossing\n");
                 }
                 else 
                 {
                 fprintf(stderr,"packed a message\n\n");
                 }
                 
                 
                 } 
                 else 
                 {
                 fprintf(stderr,"Tracker Fastrak: No valid connection\n");
                 }
                 
                 //-----------------------------*/
                
                //printf("%s\n", buffer);
                //printf("before first sync status is %d\n",status);
                status = vrpn_TRACKER_SYNCING;
                //printf("after first set sync status is %d\n",status);
                
                return 1;
            }
                //no valid alt probably RMC, safe to ignore if we care about alt
            } else {//valid lat lon alt fail
                printf("GPS cannot determine position (empty fields). Wait for satellites or reposition GPS.\n");
                //printf("GPS cannot determine position (empty fields).");
                status = vrpn_TRACKER_SYNCING;
                return 0;
            }
		} else {//valid sentence fail
            //status = vrpn_TRACKER_FAIL;
            status = vrpn_TRACKER_RESETTING;
            //is this a good place to put it?  If it's not a valid sentence? maybe it should be in reset.
            printf("Sentence Invalid.  Resetting tracker and resyncing.\n");
            return 0;
        }
        // failed valid sentence
        status = vrpn_TRACKER_SYNCING;
        return 0;
        
//#ifdef VERBOSE2
        //      print_latest_report();
//#endif
    }
    
    
    // This function should be called each time through the main loop
    // of the server code. It polls for a report from the tracker and
    // sends it if there is one. It will reset the tracker if there is
    // no data from it for a few seconds.
#if 0
    void vrpn_Tracker_GPS::mainloop()
    {
        //char temp[256];
        fprintf(stderr,"calling server main\n");
        // Call the generic server mainloop, since we are a server
        server_mainloop();
        
        //-eb adding get report and removing switch statement
        //get_report();
        
        fprintf(stderr,"status in mainloop is %d\n\n",status);
        
        switch (status) {
            case vrpn_TRACKER_REPORT_READY:
            {
                printf("tracker report ready\n",status);
                
                gettimeofday(&timestamp, NULL);	// Set watchdog now
                
                // Send the message on the connection
                if (d_connection) {
                    char	msgbuf[1000];
                    
                    
                    //sprintf(temp, "position id = %d, sender id = %d", position_m_id, d_sender_id); 
                    //MessageBox(NULL, temp,"GPS Testing",0);
                    
                    // Pack position report
                    int	len = encode_to(msgbuf);
                    if (d_connection->pack_message(len, timestamp,
                                                   position_m_id, d_sender_id, msgbuf,
                                                   vrpn_CONNECTION_LOW_LATENCY)) {
                        
                        fprintf(stderr,"Fastrak: cannot write message: tossing\n");
                    }
                    
                    // Pack velocity report
                    
                    //   len = encode_vel_to(msgbuf);
                    // if (d_connection->pack_message(len, timestamp,
                    //                                velocity_m_id, d_sender_id, msgbuf,
                    //                              vrpn_CONNECTION_LOW_LATENCY)){
                    //	  fprintf(stderr,"Fastrak: cannot write message: tossing\n");
                    //  }
                    
                    
                } else {
                    fprintf(stderr,"Tracker Fastrak: No valid connection\n");
                }
                
                // Ready for another report
                status = vrpn_TRACKER_SYNCING;
                
            }
                break;
                
            case vrpn_TRACKER_SYNCING:
            case vrpn_TRACKER_AWAITING_STATION:
            case vrpn_TRACKER_PARTIAL:
            {
                // It turns out to be important to get the report before checking
                // to see if it has been too long since the last report.  This is
                // because there is the possibility that some other device running
                // in the same server may have taken a long time on its last pass
                // through mainloop().  Trackers that are resetting do this.  When
                // this happens, you can get an infinite loop -- where one tracker
                // resets and causes the other to timeout, and then it returns the
                // favor.  By checking for the report here, we reset the timestamp
                // if there is a report ready (ie, if THIS device is still operating).
                
                
                get_report();
                // 
                // struct timeval current_time;
                // gettimeofday(&current_time, NULL);
                // if ( duration(current_time,timestamp) > MAX_TIME_INTERVAL) {
                //	  sprintf(errmsg,"Timeout... current_time=%ld:%ld, timestamp=%ld:%ld",current_time.tv_sec, current_time.tv_usec, timestamp.tv_sec, timestamp.tv_usec);
                //	  FT_ERROR(errmsg);
                //	  MessageBox(NULL,"Timeout","GPS Testing",0);
                //	  status = vrpn_TRACKER_FAIL;
                //  }
                // 
                
            }
                break;
                
            case vrpn_TRACKER_RESETTING:
                reset();
                break;
                
            case vrpn_TRACKER_FAIL:
                FT_WARNING("Tracking failed, trying to reset (try power cycle if more than 4 attempts made)");
                //vrpn_close_commport(serial_fd);
                //serial_fd = vrpn_open_commport(portname, baudrate);
                status = vrpn_TRACKER_RESETTING;
                break;
        }//switch
        
    }
#endif
