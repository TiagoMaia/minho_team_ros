//ROS includes
#include "ros/ros.h"
//Application includes
#include "minho_team_ros/hardwareInfo.h"
#include "minho_team_ros/robotInfo.h"
#include "minho_team_ros/goalKeeperInfo.h"
#include "minho_team_ros/interAgentInfo.h"
#include "minho_team_ros/position.h"
#include "std_msgs/UInt16.h"
#include <iostream>
#include <string.h>
#include <sstream>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
//defines for signal
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include<arpa/inet.h>
#include<sys/socket.h>

using namespace ros;
using minho_team_ros::hardwareInfo; //Namespace for hardwareInfo msg - SUBSCRIBING
using minho_team_ros::robotInfo; //Namespace for robotInfo msg - SUBSCRIBING
using minho_team_ros::goalKeeperInfo; //Namespace for goalKeeperInfo msg - SUBSCRIBING
using minho_team_ros::interAgentInfo; //Namespace for interAgentInfo msg - SENDING OVER UDP/SUBSCRIBING OVER UDP

// ###### GLOBAL DATA ######
// \brief subscriber for hardwareInfo message
ros::Subscriber hw_sub;
// \brief subscriber for robotInfo message
ros::Subscriber robot_sub;
// \brief subscriber for goalKeeperInfo message
ros::Subscriber gk_sub;

/// \brief variable to build the topic name for hardwareInfo message
std::stringstream hw_topic_name;
/// \brief variable to build the topic name for robotInfo message
std::stringstream robot_topic_name;
/// \brief variable to build the topic name for goalKeeperInfo message
std::stringstream gk_topic_name;
/// \brief variable to build the node name
std::stringstream node_name;

/// \brief message to store most recent hardwareInfo message received
/// and to store the most recent goalKeeperInfo or robotInfo received. 
/// Acts as dual purpose message container, as interAgentInfo
/// which will be serialized contains hardwareInfo and goalKeeperInfo
/// messages, to allow both goalkeeper and non goalkeeper agents to use
/// the same data structures
interAgentInfo message;

/// \brief shared_array of uint8 (unsigned char) used in message 
/// deserialization
boost::shared_array<uint8_t> serialization_buffer;
// #########################


// ###### SOCKET DATA#######
struct sockaddr_in si_me, si_other;
int slen = sizeof(si_other);
int socket_fd;
int port_number = 23416;
std::string multicast_address = "127.0.0.255";
// #########################


// ###### THREAD DATA ######
/// \brief a mutex to avoid multi-thread access to message
pthread_mutex_t message_mutex = PTHREAD_MUTEX_INITIALIZER;
/// \brief a mutex to avoid multi-thread access to socket
pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;
// #########################

// ### FUNCTION HEADERS ####
/// \brief callback function of hardwareInfo messages/topic of ROS
/// \param msg - message containing received data of hardwareInfo topic
void hardwareInfoCallback(const hardwareInfo::ConstPtr &msg);

/// \brief callback function of robotInfo messages/topic of ROS
/// \param msg - message containing received data of robotInfo topic
void robotInfoCallback(const robotInfo::ConstPtr &msg);

/// \brief callback function of goalKeeperInfo messages/topic of ROS
/// \param msg - message containing received data of goalKeeperInfo topic
void goalKeeperInfoCallback(const goalKeeperInfo::ConstPtr &msg);

/// \brief serializes a message to send over UDP as a string
/// of bytes.
/// \typename Message - type of ROS mesasge to serialize
/// \param msg - pointer to interAgentInfo object to be serialized
/// \param packet - uint8_t vector containing the serialized message
/// \param packet_size - size of the generated packet
/// WARN : ONLY FOR INTERAGENT MESSAGES (SENT WITH UDP)
template<typename Message>
void serializeROSMessage(Message *msg, uint8_t **packet, uint32_t *packet_size);

/// \brief deserializes a string of bytes into a message
/// \typename Message - type of ROS mesasge to deserialize
/// \param packet - uint8_t vector containing message to deserialize
/// \param msg - pointer to interAgentInfo destination object
/// WARN : ONLY FOR INTERAGENT MESSAGES (SENT WITH UDP)
template<typename Message>
void deserializeROSMessage(uint8_t *packet, void *msg);

/// \brief main thread to send robot information update over UDP socket
/// \param signal - system signal for timing purposes
static void sendRobotInformationUpdate(int signal);

/// \brief wrapper function for error and exit
/// \param msg - message to print error
void die(std::string msg);

/// \brief initializes multicast socket for sending information to 
/// multicast address
void setupSenderMultiCastSocket();
// #########################


/// \brief this node acts as a bridge between the other agents (robots and base
/// station) and each robot's ROS. It uses UDP broadcast transmission and sends
/// serialized ROS messages.
int main(int argc, char **argv)
{
   //Setup robotid and mode
   // #########################
   if(argc>1 && argc!=3) { 
      ROS_ERROR("Must enter robot id and mode as parameter for the simulated robot.\n \
                Please use -s for simulation, followed by the robot's ID.");
      exit(1); 
   }
   
   bool mode_real = true;
   int robot_id = 0;
   if(argc==3){
      robot_id = atoi(argv[2]);
      if(robot_id<0 || robot_id>6){
         ROS_ERROR("Must enter robot id correctly. Robot id's range from 1 to 6 and 0 to localhost.");
         exit(2);     
      }
      if(!strcmp(argv[1],"-s")) mode_real = false;
      else { 
         ROS_ERROR("Must enter mode correctly. Please use -s for simulation.");
         exit(3); 
      }
   }
   
   ROS_WARN("Attempting to start Coms services of coms_node.");
   node_name << "coms_node";
   if(mode_real) ROS_INFO("Running Teleop for Robot %d.",robot_id);
   else { 
      node_name << robot_id;
      ROS_INFO("Running Teleop for Robot %d in simulation.",robot_id);
   }
   // #########################
   
   //Setup Sockets
   // #########################
   setupSenderMultiCastSocket();   
	// #########################
	
	//Initialize ROS
	// #########################
	ros::init(argc, argv, node_name.str().c_str(),ros::init_options::NoSigintHandler);
	//Request node handler
	ros::NodeHandle coms_node;
	//Setup ROS
	if(!mode_real){
	   hw_topic_name << "/minho_gazebo_robot" << robot_id;
	   robot_topic_name << "/minho_gazebo_robot" << robot_id;
	   gk_topic_name << "/minho_gazebo_robot" << robot_id;
	}
	
	hw_topic_name << "/hardwareInfo";
	robot_topic_name << "/robotInfo";
	gk_topic_name << "/goalKeeperInfo";
	
	hw_sub = coms_node.subscribe(hw_topic_name.str().c_str(),
	                             1,&hardwareInfoCallback);
	robot_sub = coms_node.subscribe(robot_topic_name.str().c_str(),
	                             1,&robotInfoCallback);
	gk_sub = coms_node.subscribe(gk_topic_name.str().c_str(),
	                             1,&goalKeeperInfoCallback);
   // #########################
   
	//Setup Updated timer thread
	// #########################
	struct itimerval timer;
	signal(SIGALRM,sendRobotInformationUpdate);
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 33000;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 33000;
	// #########################
	
	// Run functions and join threads
	// #########################
	ROS_WARN("MinhoTeam coms_node started running on ROS.");
	ros::AsyncSpinner spinner(2);
	spinner.start();
	setitimer (ITIMER_REAL, &timer, NULL);
	while(ros::ok());
	pthread_exit(NULL);
	// #########################
}

// ###### FUNCTIONS ########
/// \brief callback function of hardwareInfo messages/topic of ROS
/// \param msg - message containing received data of hardwareInfo topic
void hardwareInfoCallback(const hardwareInfo::ConstPtr &msg)
{
   pthread_mutex_lock (&message_mutex); //Lock mutex
   message.hardware_info = (*msg);
   pthread_mutex_unlock (&message_mutex); //Unlock mutex
}

/// \brief callback function of robotInfo messages/topic of ROS
/// Sets the robotInfo portion of goalKeeperInfo message contained
/// within interAgentInfo. Sets is_goalkeeper flag to false
/// \param msg - message containing received data of robotInfo topic
void robotInfoCallback(const robotInfo::ConstPtr &msg)
{
   pthread_mutex_lock (&message_mutex); //Lock mutex
   message.agent_info.robot_info = (*msg); 
   message.is_goalkeeper = false;  
   pthread_mutex_unlock (&message_mutex); //Unlock mutex
}

/// \brief callback function of goalKeeperInfo messages/topic of ROS
/// Sets the goalKeeperInfo within interAgentInfo. 
/// Sets is_goalkeeper flag to true
/// \param msg - message containing received data of goalKeeperInfo topic
void goalKeeperInfoCallback(const goalKeeperInfo::ConstPtr &msg)
{
   pthread_mutex_lock (&message_mutex); //Lock mutex
   message.agent_info = (*msg);  
   message.is_goalkeeper = true;  
   pthread_mutex_unlock (&message_mutex); //Unlock mutex
}

/// \brief serializes a message to send over UDP as a string
/// of bytes.
/// \typename Message - type of ROS mesasge to serialize
/// \param msg - pointer to interAgentInfo object to be serialized
/// \param packet - uint8_t vector containing the serialized message
/// \param packet_size - size of the generated packet
/// WARN : ONLY FOR INTERAGENT MESSAGES (SENT WITH UDP)
template<typename Message>
void serializeROSMessage(Message *msg, uint8_t **packet,  uint32_t *packet_size)
{  
   pthread_mutex_lock (&message_mutex); //Lock mutex
   uint32_t serial_size = ros::serialization::serializationLength( *msg );
   serialization_buffer.reset(new uint8_t[serial_size]);
   msg->packet_size = serial_size; 
   (*packet_size) = serial_size;
   ros::serialization::OStream stream( serialization_buffer.get(), serial_size );
   ros::serialization::serialize( stream, *msg);
   (*packet) = serialization_buffer.get();
	pthread_mutex_unlock (&message_mutex); //Unlock mutex
}

/// \brief deserializes a string of bytes into a message
/// \typename Message - type of ROS mesasge to deserialize
/// \param packet - uint8_t vector containing message to deserialize
/// \param msg - pointer to interAgentInfo destination object
/// WARN : ONLY FOR INTERAGENT MESSAGES (SENT WITH UDP)
template<typename Message>
void deserializeROSMessage(uint8_t *packet, Message *msg)
{  
   std_msgs::UInt16 packet_size;
   ros::serialization::IStream psize_stream(packet, 2);
   ros::serialization::deserialize(psize_stream, packet_size);
   ros::serialization::IStream istream(packet, packet_size.data);
   ros::serialization::deserialize(istream, *msg);
}

/// \brief main thread to send robot information update over UDP socket
/// \param signal - system signal for timing purposes
static void sendRobotInformationUpdate(int signal)
{
   if(signal==SIGALRM){
      uint8_t *packet;
      uint32_t packet_size;
      serializeROSMessage<interAgentInfo>(&message,&packet,&packet_size);
      // Send packet of size packet_size through UDP
      pthread_mutex_lock (&socket_mutex); //Lock mutex
      if (sendto(socket_fd, packet, packet_size , 0 , (struct sockaddr *) &si_other, slen) < 0){
         die("Failed to send a packet.");
      }
      pthread_mutex_unlock (&socket_mutex); //Unlock mutex
	}
}

/// \brief wrapper function for error and exit
/// \param msg - message to print error
void die(std::string msg) {
    ROS_ERROR("%s",msg.c_str());
    exit(0);
}

/// \brief initializes multicast socket for sending information to 
/// multicast address
void setupSenderMultiCastSocket()
{
   if ((socket_fd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
      die("Failed to create UDP socket.");
   }
   
   memset((char *) &si_other, 0, sizeof(si_other));
   si_other.sin_family = AF_INET;
   si_other.sin_port = htons(port_number);
   if (inet_aton(multicast_address.c_str() , &si_other.sin_addr) == 0){
      die("Failed to set multicast address.");
   }
   
   ROS_INFO("UDP Multicast System started.");
}
// #########################
