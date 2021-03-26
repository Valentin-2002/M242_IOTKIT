/** Beispiel Abfrage Cloud Dienst Sunrise / Sunset
 */
#include "mbed.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "http_request.h"
#include "MbedJSONValue.h"
#include "PinNames.h"
#include "mbed_thread.h"

#if MBED_CONF_IOTKIT_HTS221_SENSOR == true
#include "HTS221Sensor.h"
#endif
#if MBED_CONF_IOTKIT_BMP180_SENSOR == true
#include "BMP180Wrapper.h"
#endif
#include "OLEDDisplay.h"

static DevI2C devI2c( MBED_CONF_IOTKIT_I2C_SDA, MBED_CONF_IOTKIT_I2C_SCL );
#if MBED_CONF_IOTKIT_HTS221_SENSOR == true
static HTS221Sensor hum_temp(&devI2c);
#endif
#if MBED_CONF_IOTKIT_BMP180_SENSOR == true
static BMP180Wrapper hum_temp( &devI2c );
#endif

// Assign Pins for Stepper Motor
DigitalOut s1( D8 );
DigitalOut s2( D9 );
DigitalOut s3( D12 );
DigitalOut s4( D13 );

DigitalOut en1( D10 );
DigitalOut en2( D11 );

// Time to wait between every pole switch in the stepper motor
static int stepSleepMS = 1;

// Current Position of the Stepper Motor
static int stepperPosition = 0;

// Maximal allowed Position of the Stepper Motor
static int maxPosition = 2000;

// Minutes to wait after executing the loop
static int sleepForMins = 5;

// Amount of Positions for the Window
static int stepAmount = 5;

// ID for the Sensor
uint8_t id;

// Temparature value as float comma value
float rawMeasuredTemp;

// Preferred Temparature Value defined by the User recieved from the Rest API
int responseTemp;

// Helper Method wich performs a single Step of the Stepper Motor counter clockwise
void stepCounterClockWise () {
    en1 = 1; en2 = 1;
    s1 = 1; s2 = 0; s3 = 1; s4 = 0;
    thread_sleep_for( stepSleepMS );
    s3 = 0; s4 = 1;                 
    thread_sleep_for( stepSleepMS );
    s1 = 0; s2 = 1;                
    thread_sleep_for( stepSleepMS );
    s3 = 1; s4 = 0;                 
    thread_sleep_for( stepSleepMS );
    stepperPosition--;
}

// Helper Method wich performs a single Step of the Stepper Motor clockwise
void stepClockwise() {
    en1 = 1; en2 = 1;
    s1 = 0; s2 = 1; s3 = 1; s4 = 0;
    thread_sleep_for( stepSleepMS );
    s3 = 0; s4 = 1;                 
    thread_sleep_for( stepSleepMS );
    s1 = 1; s2 = 0;                 
    thread_sleep_for( stepSleepMS );
    s3 = 1; s4 = 0;                 
    thread_sleep_for( stepSleepMS );
    stepperPosition++;
}

int main()
{

    /* Init all sensors with default params */
    hum_temp.init(NULL);
    hum_temp.enable();
    hum_temp.read_id(&id);

    // Connect to the network with the default networking interface
    // if you use WiFi: see mbed_app.json for the credentials
    WiFiInterface* network = WiFiInterface::get_default_instance();
    if (!network) {
        printf("ERROR: No WiFiInterface found.\n");
        return -1;
    }
    printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
    int ret = network->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
        printf("\nConnection error: %d\n", ret);
        return -1;
    }
    printf("Success\n\n");
    printf("MAC: %s\n", network->get_mac_address());
    SocketAddress a;
    network->get_ip_address(&a);
    printf("IP: %s\n", a.get_ip_address()); 

    // Move the Window to the Middle
    for(int i  = 0; i <= (maxPosition / 2); i++) {
        stepClockwise();
    } 

    while( 1 )
    {        

        // Correct stepper motor position if position is higher than max value or lower than 0
        if(stepperPosition < 0) {
            printf("%i", abs(stepperPosition));
            for(int i  = 0; i <= abs(stepperPosition); i++) {
                printf("+\n");
                stepClockwise();
            }
        } else if(stepperPosition > maxPosition) {
            for(int i  = 0; i <= stepperPosition - maxPosition; i++) {
                printf("-\n");
                stepCounterClockWise();
            }
        }

        // Get Temparature Value
        hum_temp.get_temperature(&rawMeasuredTemp);
        int measuredTemp = (int) rawMeasuredTemp;
        char req_temp[8];

        // Perform request to Rest API to recieve the preferred Temparature from the user as well as transmitting the measured Value to the Database
        sprintf(req_temp, "%d", measuredTemp);
        char req_url[] = "http://ocamumik.myhostpoint.ch/api.php?token=CWGJlK8nDxkRq4h&temp=";
        strcat(req_url, req_temp);

        // By default the body is automatically parsed and stored in a buffer, this is memory heavy.
        // To receive chunked response, pass in a callback as last parameter to the constructor.
        HttpRequest* get_req = new HttpRequest( network, HTTP_GET, req_url);

        // Get and format the Response
        HttpResponse* get_res = get_req->send();
        if (!get_res) {
            printf("HttpRequest failed (error code %d)\n", get_req->get_error());
            return 1;
        }

        responseTemp = std::stoi( get_res->get_body_as_string().c_str() );

        // Opens the Window for a defined distance (stepAmount) if measured temp is hotter than the preferred temp or closes it if's colder
        if(measuredTemp != responseTemp) {
            if(measuredTemp < responseTemp) {
                if(stepperPosition <= maxPosition && stepperPosition > 0) {
                    for(int i = 0; i <= (maxPosition / stepAmount); i++) {
                        stepCounterClockWise();
                    }
                }
            } else {
                if(stepperPosition >= 0 && stepperPosition < maxPosition) {
                    for(int i = 0; i <= (maxPosition / stepAmount); i++) {
                        stepClockwise();
                    }
                }
            }
        }

        // Print relevant Values in the Console to help debugging
        printf( "temp: %i\nres: %i\npos: %i\n\n", measuredTemp, responseTemp, stepperPosition);

        // Dump the Request
        delete get_req;

        // Wait a defined Time in Minutes until the loops starts over
        thread_sleep_for( 1000 * 60 * sleepForMins );
    }
}
