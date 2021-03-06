#include "configserver.h"

ConfigServer::ConfigServer(ros::NodeHandle *par , QObject *parent) : QObject(parent)
{
   n_subscriptions_ = 0;  
   multiple_sends_ = false;
   configured_frequency_ = 1;
   parent_ = par;
   
   watchdog_timer_ = new QTimer(); 
   image_timer_ = new QTimer();
   connect(watchdog_timer_,SIGNAL(timeout()),this,SLOT(getSubscribers()));
   connect(image_timer_,SIGNAL(timeout()),this,SLOT(postRequestedImage()));
   
   init_mock_image();
   // Setup ROS publishers and subscribers
   it_ = new image_transport::ImageTransport(*par);
   image_pub_ = it_->advertise("camera/image", 1);
   mirror_sub_ = par->subscribe("mirrorConfig", 
                                  100, 
                                  &ConfigServer::processMirrorConfig,
                                  this);
   vision_sub_ = par->subscribe("visionHSVConfig", 
                                  100, 
                                  &ConfigServer::processVisionConfig,
                                  this);
   image_sub_ = par->subscribe("imageConfig", 
                                  100, 
                                  &ConfigServer::processImageConfig,
                                  this);
                                  
   service_omniconf = par->advertiseService("requestOmniVisionConf",
                                  &ConfigServer::omniVisionConfService,
                                  this);
                                  
   service_imgrequest = par->advertiseService("requestImage",
                                  &ConfigServer::processImageRequest,
                                  this);
   watchdog_timer_->start(100);
   ROS_WARN("ConfigServer running on ROS ...");
}


ConfigServer::~ConfigServer()
{
   watchdog_timer_->stop();
   image_timer_->stop();
   image_pub_.shutdown();
}

void ConfigServer::assignImage(Mat *source)
{
   image_ = cv_bridge::CvImage(std_msgs::Header(), "bgr8", *source).toImageMsg();
   if(!image_timer_->isActive())image_timer_->start(1000.0/(float)configured_frequency_);
}

void ConfigServer::setOmniVisionConf(mirrorConfig mirrmsg,visionHSVConfig vismsg, imageConfig imgmsg)
{
   mirrorConfmsg = mirrmsg;
   visionConfmsg = vismsg;
   imageConfmsg = imgmsg;
}
void ConfigServer::getSubscribers()
{
   n_subscriptions_ = image_pub_.getNumSubscribers();
   if(n_subscriptions_<=0 && image_timer_->isActive()){
       emit stopImageAssigning();
       image_timer_->stop();
   }
}

bool ConfigServer::processImageRequest(requestImage::Request &req, requestImage::Response &res)
{
   emit changedImageRequest(req.type); // Tells the upper class to assign Images
   configured_frequency_ = req.frequency;
   if(configured_frequency_<=0) configured_frequency_ = 1;
   if(configured_frequency_>30) configured_frequency_ = 30;
   multiple_sends_ = req.is_multiple;
   if(!multiple_sends_) configured_frequency_ = 30; // This is to send the single image as fast as possible
   
   res.success = true;
   return true;
}

void ConfigServer::processMirrorConfig(const mirrorConfig::ConstPtr &msg)
{
   emit changedMirrorConfiguration(msg);
   mirrorConfmsg = *msg;
}

void ConfigServer::processVisionConfig(const visionHSVConfig::ConstPtr &msg)
{
   emit changedLutConfiguration(msg);
   visionConfmsg = *msg;
}

void ConfigServer::processImageConfig(const imageConfig::ConstPtr &msg)
{
   emit changedImageConfiguration(msg);
   imageConfmsg = *msg;
}
  
bool ConfigServer::omniVisionConfService(minho_team_ros::requestOmniVisionConf::Request &req,minho_team_ros::requestOmniVisionConf::Response &res)
{
   ROS_INFO("requestOmniVisionConf by %s",req.request_node_name.c_str());
   //get data from 
   res.mirrorConf = mirrorConfmsg;
   res.visionConf = visionConfmsg;
   res.imageConf = imageConfmsg;
   return true;
}
                               
void ConfigServer::postRequestedImage()
{
   image_timer_->stop();
   image_pub_.publish(image_); 
   if(multiple_sends_)image_timer_->start(1000.0/(float)configured_frequency_);
   else { emit stopImageAssigning();}
}

void ConfigServer::init_mock_image()
{
   mock_image = cv::Mat(480,480,CV_8UC3,cv::Scalar(100,100,100));
   cv::putText(mock_image,"NO IMAGE",Point(10,240),FONT_HERSHEY_SIMPLEX,3.0,cv::Scalar(0,255,0),4);
   image_ = cv_bridge::CvImage(std_msgs::Header(), "bgr8", mock_image).toImageMsg();
}
