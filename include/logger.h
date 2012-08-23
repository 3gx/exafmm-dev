#ifndef logger_h
#define logger_h
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/time.h>
#include "types.h"

//! Timer and Trace logger
class Logger {
private:
  std::ofstream   timerFile;                                    //!< File ID to store log
  Timer           beginTimer;                                   //!< Timer base value
  Timer           timer;                                        //!< Stores timings for all events
  Traces          traces;                                       //!< Stores traces for all events
  pthread_mutex_t mutex;                                        //!< Pthread communicator

//! Timer function
  double get_time() const {
    struct timeval tv;                                          // Time value
    gettimeofday(&tv, NULL);                                    // Get time of day in seconds and microseconds
    return double(tv.tv_sec+tv.tv_usec*1e-6);                   // Combine seconds and microseconds and return
  }

public:
  int stringLength;                                             //!< Max length of event name
  bool printNow;                                                //!< Switch to print timings

//! Constructor
  Logger() : timerFile("time.dat"),                             // Open timer log file
             beginTimer(), timer(), traces(), mutex(),          // Initializing class variables (empty)
             stringLength(20),                                  // Max length of event name
             printNow(false) {                                  // Don't print timings by default
    pthread_mutex_init(&mutex,NULL);                            // Initialize pthread communicator
  }
//! Destructor
  ~Logger() {
    timerFile.close();                                          // Close timer log file
  }

//! Start timer for given event
  inline void startTimer(std::string event) {
    beginTimer[event] = get_time();                             // Get time of day and store in beginTimer
  }

//! Stop timer for given event
  double stopTimer(std::string event, bool print=false) {
    double endTimer = get_time();                               // Get time of day and store in endTimer
    timer[event] += endTimer - beginTimer[event];               // Accumulate event time to timer
    if (print) std::cout << std::setw(stringLength) << std::left// Set format
                        << event << " : " << timer[event] << std::endl;// Print event and timer to screen
    return endTimer - beginTimer[event];                        // Return the event time
  }

//! Erase entry in timer
  inline void eraseTimer(std::string event) {
    timer.erase(event);                                         // Erase event from timer
  }

//! Erase all events in timer
  inline void resetTimer() {
    timer.clear();                                              // Clear timer
  }

//! Print timings of a specific event
  inline void printTime(std::string event) {
    std::cout << std::setw(stringLength) << std::left           // Set format
              << event << " : " << timer[event] << std::endl;   // Print event and timer
  }

//! Print timings of all events
  inline void printAllTime() {
    for (TI_iter E=timer.begin(); E!=timer.end(); E++) {        // Loop over all events
      std::cout << std::setw(stringLength) << std::left         //  Set format
                << E->first << " : " << E->second << std::endl; //  Print event and timer
    }                                                           // End loop over all events
  }

//! Write timings of all events
  inline void writeTime() {
    for (TI_iter E=timer.begin(); E!=timer.end(); E++) {        // Loop over all events
      timerFile << std::setw(stringLength) << std::left         //  Set format
                << E->first << " " << E->second << std::endl;   //  Print event and timer
    }                                                           // End loop over all events
  }

//! Start PAPI event
  inline void startPAPI() {
#if PAPI
    int events[3] = { PAPI_L2_DCM, PAPI_L2_DCA, PAPI_TLB_DM };  // PAPI event type
    PAPI_library_init(PAPI_VER_CURRENT);                        // PAPI initialize
    PAPI_create_eventset(&PAPIEVENT);                           // PAPI create event set
    PAPI_add_events(PAPIEVENT, events, 3);                      // PAPI add events
    PAPI_start(PAPIEVENT);                                      // PAPI start
#endif
  }

//! Stop PAPI event
  inline void stopPAPI() {
#if PAPI
    long long values[3] = {0,0,0};                              // Values for each event
    PAPI_stop(PAPIEVENT,values);                                // PAPI stop
    std::cout << std::setw(stringLength) << std::left           //  Set format
              << "L2 Miss" << " : " << values[0] << std::endl;  //  Print PAPI event and count
    std::cout << "L2 Miss: " << values[0]                       // Print L2 Misses
              << " L2 Access: " << values[1]                    // Print L2 Access
              << " TLB Miss: " << values[2] << std::endl;       // Print TLB Misses
#endif
  }

//! Start tracer for given event
  inline void startTracer(ThreadTrace &beginTrace) {
    pthread_mutex_lock(&mutex);                                 // Lock shared variable access
    beginTrace[pthread_self()] = get_time();                    // Get time of day and store in beginTrace
    pthread_mutex_unlock(&mutex);                               // Unlock shared variable access
  }

//! Stop tracer for given event
  inline void stopTracer(ThreadTrace &beginTrace, int color) {
    pthread_mutex_lock(&mutex);                                 // Lock shared variable access
    Trace trace;                                                // Define trace structure
    trace.thread = pthread_self();                              // Store pthread id
    trace.begin  = beginTrace[pthread_self()];                  // Store tic
    trace.end    = get_time();                                  // Store toc
    trace.color  = color;                                       // Store color of event
    traces.push(trace);                                         // Push trace to queue of traces
    pthread_mutex_unlock(&mutex);                               // Unlock shared variable access
  }

//! Write traces of all events
  inline void writeTrace(int mpirank) {
    char fname[256];                                            // File name
    sprintf(fname,"trace%4.4d.svg",mpirank);                    // Create file name for trace
    std::ofstream traceFile(fname);                             // Open trace log file
    traceFile << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" // Header statements for trace log file
        << "<!DOCTYPE svg PUBLIC \"-_W3C_DTD SVG 1.0_EN\" \"http://www.w3.org/TR/SVG/DTD/svg10.dtd\">\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
        << "  width=\"200mm\" height=\"40mm\" viewBox=\"0 0 20000 4000\">\n"
        << "  <g>\n";
    int num_thread = 0;                                         // Counter for number of threads to trace
    ThreadMap threadMap;                                        // Map pthread ID to thread ID
    double base = traces.front().begin;                         // Base time
    double scale = 30000.0;                                     // Scale the length of bar plots
    while (!traces.empty()) {                                   // While queue of traces is not empty
      Trace trace = traces.front();                             //  Get trace at front of the queue
      traces.pop();                                             //  Pop trace at front
      pthread_t thread = trace.thread;                          //  Get pthread ID of trace
      double begin  = trace.begin;                              //  Get begin time of trace
      double end    = trace.end;                                //  Get end time of trace
      int    color  = trace.color;                              //  Get color of trace
      if (threadMap[thread] == 0) {                             //  If it's a new pthread ID
        threadMap[thread] = ++num_thread;                       //   Map it to an incremented thread ID
      }                                                         //  End if for new pthread ID
      begin -= base;                                            //  Subtract base time from begin time
      end   -= base;                                            //  Subtract base time from end time
      traceFile << "    <rect x=\"" << begin * scale            //  x position of bar plot
          << "\" y=\"" << (threadMap[thread] - 1) * 100.0       //  y position of bar plot
          << "\" width=\"" << (end - begin) * scale             //  width of bar
          << "\" height=\"90.0\" fill=\"#"<< std::setfill('0') << std::setw(6) << std::hex << color// height of bar
          << "\" stroke=\"#000000\" stroke-width=\"1\"/>\n";    //  stroke color and width
    }                                                           // End while loop for queue of traces
    traceFile << "  </g>\n" "</svg>\n";                         // Footer for trace log file 
    traceFile.close();                                          // Close trace log file
  }
};

#endif
