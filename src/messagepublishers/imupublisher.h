
//  ==> COPYRIGHT (C) 2019 XSENS TECHNOLOGIES OR SUBSIDIARIES WORLDWIDE <==
//  WARNING: COPYRIGHT (C) 2019 XSENS TECHNOLOGIES OR SUBSIDIARIES WORLDWIDE. ALL RIGHTS RESERVED.
//  THIS FILE AND THE SOURCE CODE IT CONTAINS (AND/OR THE BINARY CODE FILES FOUND IN THE SAME
//  FOLDER THAT CONTAINS THIS FILE) AND ALL RELATED SOFTWARE (COLLECTIVELY, "CODE") ARE SUBJECT
//  TO AN END USER LICENSE AGREEMENT ("AGREEMENT") BETWEEN XSENS AS LICENSOR AND THE AUTHORIZED
//  LICENSEE UNDER THE AGREEMENT. THE CODE MUST BE USED SOLELY WITH XSENS PRODUCTS INCORPORATED
//  INTO LICENSEE PRODUCTS IN ACCORDANCE WITH THE AGREEMENT. ANY USE, MODIFICATION, COPYING OR
//  DISTRIBUTION OF THE CODE IS STRICTLY PROHIBITED UNLESS EXPRESSLY AUTHORIZED BY THE AGREEMENT.
//  IF YOU ARE NOT AN AUTHORIZED USER OF THE CODE IN ACCORDANCE WITH THE AGREEMENT, YOU MUST STOP
//  USING OR VIEWING THE CODE NOW, REMOVE ANY COPIES OF THE CODE FROM YOUR COMPUTER AND NOTIFY
//  XSENS IMMEDIATELY BY EMAIL TO INFO@XSENS.COM. ANY COPIES OR DERIVATIVES OF THE CODE (IN WHOLE
//  OR IN PART) IN SOURCE CODE FORM THAT ARE PERMITTED BY THE AGREEMENT MUST RETAIN THE ABOVE
//  COPYRIGHT NOTICE AND THIS PARAGRAPH IN ITS ENTIRETY, AS REQUIRED BY THE AGREEMENT.
//  
//  THIS SOFTWARE CAN CONTAIN OPEN SOURCE COMPONENTS WHICH CAN BE SUBJECT TO 
//  THE FOLLOWING GENERAL PUBLIC LICENSES:
//  ==> Qt GNU LGPL version 3: http://doc.qt.io/qt-5/lgpl.html <==
//  ==> LAPACK BSD License:  http://www.netlib.org/lapack/LICENSE.txt <==
//  ==> StackWalker 3-Clause BSD License: https://github.com/JochenKalmbach/StackWalker/blob/master/LICENSE <==
//  ==> Icon Creative Commons 3.0: https://creativecommons.org/licenses/by/3.0/legalcode <==
//  

#ifndef IMUPUBLISHER_H
#define IMUPUBLISHER_H

#include <time.h>
#include "packetcallback.h"
#include <sensor_msgs/Imu.h>
#include <tf/LinearMath/Quaternion.h>
#include <tf/transform_datatypes.h>

static void variance_from_stddev_param(std::string param, double *variance_out)
{
    std::vector<double> stddev;
    if (ros::param::get(param, stddev))
    {
        if (stddev.size() == 3)
        {
            auto squared = [](double x) { return x * x; };
            std::transform(stddev.begin(), stddev.end(), variance_out, squared);
        }
        else
        {
            ROS_WARN("Wrong size of param: %s, must be of size 3", param.c_str());
        }
    }
    else
    {
        memset(variance_out, 0, 3 * sizeof(double));
    }
}

struct ImuPublisher : public PacketCallback
{
    ros::Publisher pub;
    ros::Publisher correct_pub;

    double orientation_variance[3];
    double linear_acceleration_variance[3];
    double angular_velocity_variance[3];

    bool use_utc_time = false;

    ImuPublisher(ros::NodeHandle &node)
    {
        int pub_queue_size = 5;
        ros::param::get("~publisher_queue_size", pub_queue_size);
        ros::param::get("~use_utc_time", use_utc_time);
        pub = node.advertise<sensor_msgs::Imu>("imu/data", pub_queue_size);
        //correct_pub = node.advertise<sensor_msgs::Imu>("imu_correct", pub_queue_size);

        // REP 145: Conventions for IMU Sensor Drivers (http://www.ros.org/reps/rep-0145.html)
        variance_from_stddev_param("~orientation_stddev", orientation_variance);
        variance_from_stddev_param("~angular_velocity_stddev", angular_velocity_variance);
        variance_from_stddev_param("~linear_acceleration_stddev", linear_acceleration_variance);
    }

    time_t TimeFromYMD(int year, int month, int day)
    {
      struct tm tm;
      memset(&tm, 0, sizeof(tm));
      tm.tm_year = year;
      tm.tm_mon = month;
      tm.tm_mday = day;
      return mktime(&tm);
    }

    void operator()(const XsDataPacket &packet, ros::Time timestamp)
    {
        bool quaternion_available = packet.containsOrientation();
        bool gyro_available = packet.containsCalibratedGyroscopeData();
        bool accel_available = packet.containsCalibratedAcceleration();
        bool utc_time_available = packet.containsUtcTime();

        ros::Time sample_time;
        if (utc_time_available)
        {
            XsTimeInfo time = packet.utcTime();

            double time_difference = difftime(TimeFromYMD(time.m_year, time.m_month, time.m_day), TimeFromYMD(1970, 1, 1));
            time_difference += time.m_hour*3600 + time.m_minute*60 + time.m_second + 86400; //leap 1 day

            sample_time.sec = time_difference;
            sample_time.nsec = time.m_nano;
        }

        geometry_msgs::Quaternion quaternion;
        if (quaternion_available)
        {
            XsQuaternion q = packet.orientationQuaternion();

            quaternion.w = q.w();
            quaternion.x = q.x();
            quaternion.y = q.y();
            quaternion.z = q.z();
        }

        geometry_msgs::Vector3 gyro;
        if (gyro_available)
        {
            XsVector g = packet.calibratedGyroscopeData();
            gyro.x = g[0];
            gyro.y = g[1];
            gyro.z = g[2];
        }

        geometry_msgs::Vector3 accel;
        if (accel_available)
        {
            XsVector a = packet.calibratedAcceleration();
            accel.x = a[0];
            accel.y = a[1];
            accel.z = a[2];
        }

        // Imu message, publish if any of the fields is available
        if (quaternion_available || accel_available || gyro_available)
        {
            sensor_msgs::Imu msg;
            sensor_msgs::Imu correct_msg;

            std::string frame_id = DEFAULT_FRAME_ID;
            ros::param::get("~frame_id", frame_id);

            msg.header.stamp = timestamp;
            msg.header.frame_id = frame_id;

            correct_msg.header.stamp = timestamp;
            correct_msg.header.frame_id = "base_link";

            if (utc_time_available && use_utc_time)
            {
                msg.header.stamp = sample_time;
                correct_msg.header.stamp = sample_time;
            }

            msg.orientation = quaternion;

            /*
            double temp_imuRoll, temp_imuPitch, temp_imuYaw;
            double imuRoll, imuPitch, imuYaw;
            tf::Quaternion orientation;
            tf::quaternionMsgToTF(msg.orientation, orientation);
            tf::Matrix3x3(orientation).getRPY(imuRoll, imuPitch, imuYaw);

            temp_imuRoll = imuRoll;
            temp_imuPitch = imuPitch;
            temp_imuYaw = imuYaw;
            imuRoll = temp_imuPitch;
            imuPitch = -temp_imuRoll;
            imuYaw = temp_imuYaw;

            tf::Quaternion q;
            q.setRPY(imuRoll, imuPitch, imuYaw);
            tf::quaternionTFToMsg(q, correct_msg.orientation);
            */

            if (quaternion_available)
            {
                msg.orientation_covariance[0] = orientation_variance[0];
                msg.orientation_covariance[4] = orientation_variance[1];
                msg.orientation_covariance[8] = orientation_variance[2];

                correct_msg.orientation_covariance[0] = orientation_variance[0];
                correct_msg.orientation_covariance[4] = orientation_variance[1];
                correct_msg.orientation_covariance[8] = orientation_variance[2];
            }
            else
            {
                msg.orientation_covariance[0] = -1; // mark as not available                
                correct_msg.orientation_covariance[0] = -1;
            }

            msg.angular_velocity = gyro;            
            correct_msg.angular_velocity.x = gyro.y;
            correct_msg.angular_velocity.y = -gyro.x;
            correct_msg.angular_velocity.z = gyro.z;
            if (gyro_available)
            {
                msg.angular_velocity_covariance[0] = angular_velocity_variance[0];
                msg.angular_velocity_covariance[4] = angular_velocity_variance[1];
                msg.angular_velocity_covariance[8] = angular_velocity_variance[2];

                correct_msg.angular_velocity_covariance[0] = angular_velocity_variance[0];
                correct_msg.angular_velocity_covariance[4] = angular_velocity_variance[1];
                correct_msg.angular_velocity_covariance[8] = angular_velocity_variance[2];
            }
            else
            {
                msg.angular_velocity_covariance[0] = -1; // mark as not available
                correct_msg.angular_velocity_covariance[0] = -1;
            }

            msg.linear_acceleration = accel;
            correct_msg.linear_acceleration.x = accel.y;
            correct_msg.linear_acceleration.y = -accel.x;
            correct_msg.linear_acceleration.z = accel.z;
            if (accel_available)
            {
                msg.linear_acceleration_covariance[0] = linear_acceleration_variance[0];
                msg.linear_acceleration_covariance[4] = linear_acceleration_variance[1];
                msg.linear_acceleration_covariance[8] = linear_acceleration_variance[2];

                correct_msg.linear_acceleration_covariance[0] = linear_acceleration_variance[0];
                correct_msg.linear_acceleration_covariance[4] = linear_acceleration_variance[1];
                correct_msg.linear_acceleration_covariance[8] = linear_acceleration_variance[2];
            }
            else
            {
                msg.linear_acceleration_covariance[0] = -1; // mark as not available
                correct_msg.linear_acceleration_covariance[0] = -1;
            }

            pub.publish(msg);
            //correct_pub.publish(correct_msg);
        }
    }
};

#endif
