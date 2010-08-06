/*
  Martin Galpin (m@66laps.com)
  
  Copyright (c) 2010 66laps Limited. All rights reserved.
  
  This file is part of rFactor-OpenMotorsport.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#pragma once
#ifndef CHANNELDEFINITIONS_HPP
#define CHANNELDEFINITIONS_HPP

// This file is a first step to properly externalizing channel and group names.

#define kUnitsKPH "kph"
#define kUnitsGee "g"
#define kUnitsDegrees "deg"
#define kUnitsLitres "l"
#define kUnitsRPM "rpm"
#define kUnitsCelcius "c"
#define kUnitsBoolean "boolean"
#define kUnitsPercent "%"
#define kUnitsGear "gear"
#define kUnitsMeters "m"
#define kUnitsNewtons "n"
#define kUnitsRadiansPerSecond "rad/sec"
#define kUnitsPascal "pa"

#define kGroupPosition "Position"

#define kChannelSpeed "Speed"
#define kChannelAccelerationX "Acceleration X"
#define kChannelAccelerationY "Acceleration Y"
#define kChannelAccelerationZ "Acceleration Z"
#define kChannelPitch "Pitch"
#define kChannelRoll "Roll"

#define kGroupDriver "Driver"

#define kChannelGear "Gear"
#define kChannelThrottle "Throttle"
#define kChannelBrake "Brake"
#define kChannelClutch "Clutch"
#define kChannelSteering "Steering"

#define kGroupEngine "Engine"

#define kChannelRPM "RPM"
#define kChannelClutchRPM "Clutch RPM"
#define kChannelFuel "Fuel"
#define kChannelOverheating "Overheating"

#define kGroupWheelRF "Wheel RF"
#define kGroupWheelRR "Wheel RR"
#define kGroupWheelLF "Wheel LF"
#define kGroupWheelLR "Wheel RF"

#define kChannelRotation "Rotation"
#define kChannelSuspensionDeflection "Suspension Deflection"
#define kChannelRideHeight "Right Height"
#define kChannelTireLoad "Tire Load"
#define kChannelLateralForce "Lateral Force"
#define kChannelBrakeTemperature "Brake Temperature"
#define kChannelPressure "Pressure"
#define kChannelTemperatureLeft "Temperature Left"
#define kChannelTemperatureCenter "Temperature Center"
#define kChannelTemperatureRight "Temperature Right"

#define kNumberOfWheels 4
static std::string kWheels[] = {
  kGroupWheelLF, kGroupWheelRF, 
  kGroupWheelLR, kGroupWheelRR
};

#endif /* CHANNELDEFINITIONS_HPP */