#!/bin/bash

roscore &
sleep 2
rosrun gazebo_ros gazebo &
sleep 2
rqt &
sleep 1
rosrun rover_onboard_localization localization moe &
sleep 1
rosrun rover_onboard_mobility mobility moe &
sleep 1
rosrun rover_onboard_zmapping mapping moe &
sleep 1
rosrun rover_onboard_target_detection target moe &
sleep 1
rosrun rover_driver_rqt_motor joystick_driver moe &

while true; do
echo "Quit? [q]";
read answer;
  if [ "$answer" == "q" ];then
	   pkill gzclient
           sleep 1
           pkill gzserver
           sleep 1
           pkill rqt
           sleep 1
	   pkill roslaunch
           sleep 1
	   pkill rosmaster
           sleep 1
           pkill roscore
	   sleep 1
	   pkill mobility
	   sleep 1
	   pkill mapping
	   sleep 1
	   pkill target
	   sleep 1
	   pkill localization
	   sleep 1
	   pkill joystick_driver
           exit
  else
    	   echo "Command Not Recognized..."
  fi
done
exit