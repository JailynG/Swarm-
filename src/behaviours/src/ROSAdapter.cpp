#include <ros/ros.h>

// ROS libraries
#include <angles/angles.h>
#include <random_numbers/random_numbers.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

// ROS messages
#include <std_msgs/Float32.h>
#include <std_msgs/Int16.h>
#include <std_msgs/UInt8.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/Range.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <apriltags_ros/AprilTagDetectionArray.h>
#include <std_msgs/Float32MultiArray.h>
#include "swarmie_msgs/Waypoint.h"

// Include Controllers
#include <vector>

#include "Point.h"
#include "Tag.h"

/****************
 * New Includes *
 ****************/
#include "state_machine/StateMachine.h"
#include "inputs/InputLocation.h"
#include "inputs/InputSonarArray.h"
#include "inputs/InputTags.h"
#include "waypoints/SimpleWaypoint.h"


// To handle shutdown signals so the node quits
// properly in response to "rosnode kill"
#include <ros/ros.h>
#include <signal.h>

using namespace std;


/************************
 * Global Alphabet Soup *
 ************************/

// Random number generator
random_numbers::RandomNumberGenerator* rng;

// Create logic controller

void humanTime();

// Behaviours Logic Functions
void sendDriveCommand(double linearVel, double angularVel);


// Numeric Variables for rover positioning
geometry_msgs::Pose2D current_location_odom;
geometry_msgs::Pose2D current_location_odom_accel;
geometry_msgs::Pose2D current_location_odom_accel_gps;

float linear_vel_odom_accel = 0.0;
float angular_vel_odom_accel = 0.0;

float linear_vel_odom_accel_gps = 0.0;
float angular_vel_odom_accel_gps = 0.0;

double us_left = 3.0;
double us_right = 3.0;
double us_center = 3.0;

vector<Tag> tags;

int currentMode = 0;
const float state_machines_loop = 0.1; // time between state machines function call
const float status_publish_interval = 1;
const float heartbeat_publish_interval = 2;
const float waypoint_tolerance = 0.1; //10 cm tolerance.

float prevWrist = 0;
float prevFinger = 0;
long int startTime = 0;
float minutesTime = 0;
float hoursTime = 0;

float drift_tolerance = 0.5; // meters

std_msgs::String msg;


geometry_msgs::Twist velocity;
char host[128];
char prev_state_machine[128];
// records time for delays in sequanced actions, 1 second resolution.
time_t timerStartTime;

// An initial delay to allow the rover to gather enough position data to 
// average its location.
unsigned int startDelayInSeconds = 30;
float timerTimeElapsed = 0;

// Converts the time passed as reported by ROS (which takes Gazebo simulation rate into account) into milliseconds as an integer.
long int getROSTimeInMilliSecs();

/****************************
 * END ALPHABET GLOBAL SOUP *
 ****************************/







/******************
 * ROS Publishers *
 ******************/
ros::Publisher state_machine_publish;
ros::Publisher status_publisher;
ros::Publisher finger_angle_publish;
ros::Publisher wrist_angle_publish;
ros::Publisher info_log_publisher;
ros::Publisher drive_control_publish;
ros::Publisher heartbeat_publisher;

void setupPublishers( ros::NodeHandle &ros_handle, string published_name )
{
    status_publisher = ros_handle.advertise<std_msgs::String>((published_name + "/status"), 1, true);
    state_machine_publish = ros_handle.advertise<std_msgs::String>((published_name + "/state_machine"), 1, true);
    finger_angle_publish = ros_handle.advertise<std_msgs::Float32>((published_name + "/fingerAngle/cmd"), 1, true);
    wrist_angle_publish = ros_handle.advertise<std_msgs::Float32>((published_name + "/wristAngle/cmd"), 1, true);
    info_log_publisher = ros_handle.advertise<std_msgs::String>("/infoLog", 1, true);
    drive_control_publish = ros_handle.advertise<geometry_msgs::Twist>((published_name + "/driveControl"), 10);
    heartbeat_publisher = ros_handle.advertise<std_msgs::String>((published_name + "/behaviour/heartbeat"), 1, true);
}

/*******************
 * ROS Subscribers *
 *******************/
ros::Subscriber joy_subscriber;
ros::Subscriber mode_subscriber;
ros::Subscriber target_subscriber;
ros::Subscriber odometry_subscriber;
ros::Subscriber map_subscriber;

/******************************************
 * ROS Callback Functions for Subscribers *
 ******************************************/
void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message);
void modeHandler(const std_msgs::UInt8::ConstPtr& message);
void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& tagInfo);
void odomHandler(const nav_msgs::Odometry::ConstPtr& message);
void odomAndAccelHandler(const nav_msgs::Odometry::ConstPtr& message);
void odomAccelAndGPSHandler(const nav_msgs::Odometry::ConstPtr& message);
void manualWaypointHandler(const swarmie_msgs::Waypoint& message);
void sonarHandler(const sensor_msgs::Range::ConstPtr& sonarLeft, const sensor_msgs::Range::ConstPtr& sonarCenter, const sensor_msgs::Range::ConstPtr& sonarRight);

void setupSubscribers( ros::NodeHandle &ros_handle, string published_name )
{
    joy_subscriber = ros_handle.subscribe((published_name + "/joystick"), 10, joyCmdHandler);
    mode_subscriber = ros_handle.subscribe((published_name + "/mode"), 1, modeHandler);
    target_subscriber = ros_handle.subscribe((published_name + "/targets"), 10, targetHandler);
    odometry_subscriber = ros_handle.subscribe((published_name + "/odom/filtered"), 10, odomAndAccelHandler);
    map_subscriber = ros_handle.subscribe((published_name + "/odom/ekf"), 10, odomAccelAndGPSHandler);

    //Sonar Stuff
    message_filters::Subscriber<sensor_msgs::Range> sonar_left_subscriber(ros_handle, (published_name + "/sonarLeft"), 10);
    message_filters::Subscriber<sensor_msgs::Range> sonar_center_subscriber(ros_handle, (published_name + "/sonarCenter"), 10);
    message_filters::Subscriber<sensor_msgs::Range> sonar_right_subscriber(ros_handle, (published_name + "/sonarRight"), 10);
    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Range, sensor_msgs::Range, sensor_msgs::Range> sonarSyncPolicy;
    message_filters::Synchronizer<sonarSyncPolicy> sonarSync(sonarSyncPolicy(10), sonar_left_subscriber, sonar_center_subscriber, sonar_right_subscriber);
    sonarSync.registerCallback(boost::bind(&sonarHandler, _1, _2, _3));
}

/**************
 * ROS Timers *
 **************/
ros::Timer state_machine_timer;
ros::Timer publish_status_timer;
ros::Timer publish_heartbeat_timer;

/***********************
 * ROS Timer Functions *
 ***********************/
void runStateMachines(const ros::TimerEvent&);
void publishStatusTimerEventHandler(const ros::TimerEvent& event);
void publishHeartBeatTimerEventHandler(const ros::TimerEvent& event);

void setupTimerCallbacks( ros::NodeHandle &ros_handle )
{
    publish_status_timer = ros_handle.createTimer(ros::Duration(status_publish_interval), publishStatusTimerEventHandler);
    state_machine_timer = ros_handle.createTimer(ros::Duration(state_machines_loop), runStateMachines);
    publish_heartbeat_timer = ros_handle.createTimer(ros::Duration(heartbeat_publish_interval), publishHeartBeatTimerEventHandler);
}

/******************
 * SIGINT Handler *
 ******************/
void sigintEventHandler(int signal);

/***********************
 * Logic State Machine *
 ***********************/

StateMachine logic_machine;

void setupLogicMachine()
{
    InputLocation *io_odom = new InputLocation( &current_location_odom );
    InputLocation *io_odom_accel = new InputLocation( &current_location_odom_accel );
    InputLocation *io_odom_accel_gps = new InputLocation( &current_location_odom_accel_gps );
    InputSonarArray *io_sonar_array = new InputSonarArray( &us_left, &us_right, &us_center );
    InputTags *io_tags = new InputTags( &tags );
    IOFloat *io_linear_vel_oa = new IOFloat( &linear_vel_odom_accel );
    IOFloat *io_angular_vel_oa = new IOFloat( &angular_vel_odom_accel );
    IOFloat *io_linear_vel_oag = new IOFloat( &linear_vel_odom_accel_gps );
    IOFloat *io_angular_vel_oag = new IOFloat( &angular_vel_odom_accel_gps );


    logic_machine.addInput( "odom", io_odom );
    logic_machine.addInput( "odom_accel", io_odom_accel );
    logic_machine.addInput( "odom_accel_gps", io_odom_accel_gps );
    logic_machine.addInput( "sonar", io_sonar_array );
    logic_machine.addInput( "tags", io_tags );
    logic_machine.addInput( "linear_vel_oa", io_linear_vel_oa );
    logic_machine.addInput( "angular_vel_oa", io_angular_vel_oa );
    logic_machine.addInput( "linear_vel_oag", io_linear_vel_oag );
    logic_machine.addInput( "angular_vel_oag", io_angular_vel_oag );

    /* add States */

    return;
}





/*****************
 * MAIN FUNCTION *
 *****************/

int main(int argc, char **argv)
{
    gethostname(host, sizeof (host));
    string hostname(host);
    string published_name;

    //Determine this Rover's name
    if (argc >= 2)
    {
        published_name = argv[1];
        cout << "Welcome to the world of tomorrow " << published_name
             << "!  Behaviour turnDirectionule started." << endl;
    }
    else
    {
        published_name = hostname;
        cout << "No Name Selected. Default is: " << published_name << endl;
    }

    // NoSignalHandler so we can catch SIGINT ourselves and shutdown the node
    ros::init(argc, argv, (published_name + "_BEHAVIOUR"), ros::init_options::NoSigintHandler);
    ros::NodeHandle ros_handle;

    // Register the SIGINT event handler so the node can shutdown properly
    signal(SIGINT, sigintEventHandler);

    setupSubscribers( ros_handle, published_name );
    setupPublishers( ros_handle, published_name );
    setupTimerCallbacks( ros_handle );
    setupLogicMachine();

    //TBD How to wrap this section up
    std_msgs::String msg;
    msg.data = "Log Started";
    info_log_publisher.publish(msg);

    stringstream ss;
    ss << "Rover start delay set to " << startDelayInSeconds << " seconds";
    msg.data = ss.str();
    info_log_publisher.publish(msg);
    timerStartTime = time(0);

    ros::spin();

    return EXIT_SUCCESS;
}








/************************
 * Subsequent Functions *
 ************************/

// This is the top-most logic control block organised as a state machine.
// This function calls the dropOff, pickUp, and search controllers.
// This block passes the goal location to the proportional-integral-derivative
// controllers in the abridge package.
void runStateMachines(const ros::TimerEvent&)
{
    // time since timerStartTime was set to current time
    //timerTimeElapsed = time(0) - timerStartTime;

    // Robot is in automode
    if (currentMode == 2 || currentMode == 3)
    {
        Waypoint *current_waypoint = 0;
        IOInt *left = 0;
        IOInt *right = 0;

        logic_machine.run();
        current_waypoint = logic_machine.getOutput( "current_waypoint" );

        /* TODO: add else messaging */
        if( current_waypoint && is_io_valid( current_waypoint, iowp_validator ) )
        {
            current_waypoint->run();

            left = (IOInt *)current_waypoint->getOutput( "left_velocity" );
            right = (IOInt *)current_waypoint->getOutput( "right_velocity" );

            /* TODO: add else messaging */
            if( ( left && right ) && ( is_io_valid( left, ioint_validator ) && is_io_valid( right, ioint_validator ) )
                sendDriveCommand( left->getValue(), right->getValue() );
        }

    }
    else
    {
        /* some output about manual mode? */
    }
}

void sendDriveCommand(double left, double right)
{
    velocity.linear.x = left,
    velocity.angular.z = right;
    // publish the drive commands
    drive_control_publish.publish(velocity);
}

/*************************
 * ROS CALLBACK HANDLERS *
 *************************/

void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& message)
{
    // Don't pass April tag data to the logic controller if the robot is not in autonomous mode.
    // This is to make sure autonomous behaviours are not triggered while the rover is in manual mode.
    if(currentMode == 0 || currentMode == 1)
        return;

    if (message->detections.size() > 0)
    {
        tags.clear();

        for (int i = 0; i < message->detections.size(); i++)
        {
            // Package up the ROS AprilTag data into our own type that does not rely on ROS.
            Tag loc;
            loc.setID( message->detections[i].id );

            // Pass the position of the AprilTag
            geometry_msgs::PoseStamped tagPose = message->detections[i].pose;
            loc.setPosition( make_tuple( tagPose.pose.position.x,
                                         tagPose.pose.position.y,
                                         tagPose.pose.position.z ) );

            // Pass the orientation of the AprilTag
            loc.setOrientation( ::boost::math::quaternion<float>( tagPose.pose.orientation.x,
                                                                  tagPose.pose.orientation.y,
                                                                  tagPose.pose.orientation.z,
                                                                  tagPose.pose.orientation.w ) );
            tags.push_back(loc);
        }
    }
}

void modeHandler(const std_msgs::UInt8::ConstPtr& message)
{
    currentMode = message->data;
    sendDriveCommand(0.0, 0.0);
}

/* this is awkward... do nothing for now*/
void sonarHandler(const sensor_msgs::Range::ConstPtr& sonar_left, const sensor_msgs::Range::ConstPtr& sonar_center, const sensor_msgs::Range::ConstPtr& sonar_right)
{
    us_left = sonar_left->range;
    us_right = sonar_right->range;
    us_center = sonar_center->range;
}

void odomHandler(const nav_msgs::Odometry::ConstPtr& message)
{
    current_location_odom.x = message->pose.pose.position.x;
    current_location_odom.y = message->pose.pose.position.y;
}
void odomAndAccelHandler(const nav_msgs::Odometry::ConstPtr& message)
{
    //Get (x,y) location directly from pose
    current_location_odom_accel.x = message->pose.pose.position.x;
    current_location_odom_accel.y = message->pose.pose.position.y;

    //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
    tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
    tf::Matrix3x3 m(q);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);
    current_location_odom_accel.theta = yaw;

    linear_vel_odom_accel = message->twist.twist.linear.x;
    angular_vel_odom_accel = message->twist.twist.angular.z;
}

void odomAccelAndGPSHandler(const nav_msgs::Odometry::ConstPtr& message)
{
  //Get (x,y) location directly from pose
  current_location_odom_accel_gps.x = message->pose.pose.position.x;
  current_location_odom_accel_gps.y = message->pose.pose.position.y;

  //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
  tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  current_location_odom_accel_gps.theta = yaw;

  linear_vel_odom_accel_gps = message->twist.twist.linear.x;
  angular_vel_odom_accel_gps = message->twist.twist.angular.z;
}

void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message)
{
    const int max_motor_cmd = 255;

    if (currentMode == 0 || currentMode == 1)
    {
        float linear  = abs(message->axes[4]) >= 0.1 ? message->axes[4]*max_motor_cmd : 0.0;
        float angular = abs(message->axes[3]) >= 0.1 ? message->axes[3]*max_motor_cmd : 0.0;

        float left = linear - angular;
        float right = linear + angular;

        if(left > max_motor_cmd)
            left = max_motor_cmd;
        else if(left < -max_motor_cmd)
            left = -max_motor_cmd;

        if(right > max_motor_cmd)
            right = max_motor_cmd;
        else if(right < -max_motor_cmd)
            right = -max_motor_cmd;

        sendDriveCommand(left, right);
    }
}

void publishStatusTimerEventHandler(const ros::TimerEvent&)
{
    std_msgs::String msg;
    msg.data = "online";
    status_publisher.publish(msg);
}

void sigintEventHandler(int sig)
{
  // All the default sigint handler does is call shutdown()
  ros::shutdown();
}

void publishHeartBeatTimerEventHandler(const ros::TimerEvent&)
{
  std_msgs::String msg;
  msg.data = "";
  heartbeat_publisher.publish(msg);
}

long int getROSTimeInMilliSecs()
{
  // Get the current time according to ROS (will be zero for simulated clock until the first time message is recieved).
  ros::Time t = ros::Time::now();

  // Convert from seconds and nanoseconds to milliseconds.
  return t.sec*1e3 + t.nsec/1e6;
}


void humanTime() {
  float timeDiff = (getROSTimeInMilliSecs()-startTime)/1e3;
  if (timeDiff >= 60) {
    minutesTime++;
    startTime += 60  * 1e3;
    if (minutesTime >= 60) {
      hoursTime++;
      minutesTime -= 60;
    }
  }
  timeDiff = floor(timeDiff*10)/10;

  double intP, frac;
  frac = modf(timeDiff, &intP);
  timeDiff -= frac;
  frac = round(frac*10);
  if (frac > 9) {
    frac = 0;
  }
  //cout << "System has been Running for :: " << hoursTime << " : hours " << minutesTime << " : minutes " << timeDiff << "." << frac << " : seconds" << endl; //you can remove or comment this out it just gives indication something is happening to the log file
}
