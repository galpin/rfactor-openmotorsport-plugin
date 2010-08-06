/*******************************************************************************
 * Martin Galpin (m@66laps.com)
 *
 * Copyright (c) 2010 66laps Limited.
 *
 * All rights reserved.
 *
 * This file is part of rFactor-OpenMotorsport.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 ******************************************************************************/
/*
A few notes on when we start logging data.

We apply a little bit of "non-obvious" logic to when we actually start logging
data in order to record only data that is "good" and "clean". As such, the
following logic is applied in cooperation with the scoring updates.

1. If we enter real-time mode (driving) before the session or with a green
or yellow flag, we start logging immediately. Effectively, this means that we
start logging data immediately whenever we are not entering the *start* of a 
race (so testing, practise, qualifying, warm-up). This applies to:
  
  ScoringInfoV2.mGamePhase = kGamePhaseBeforeSession
  ScoringInfoV2.mGamePhase = kGamePhaseGreenFlag | kGamePhaseFullCourseYellow

2. If we enter real-time mode when the game is in any other phase, we don't 
start logging data until the game phase is passes kGamePhaseGreenFlag AND the 
vehicles last lap start time is > 0. This is to prevent logging lap markers for
the formation lap (which could be skipped) and ending up with laptimes that are
effectively erroneous (this is reality in real life but if we can avoid it, we 
might as well). The check on the lap start time is applied because rFactor
considers the formation lap AND the first racing lap to be lap number 0. This 
means that the only way to differentiate between the formation lap and the first
racing lap is by VehicleScoringInfoV2.mLapStartingET.


A few notes on ScoringInfo.
  
1. It's not possible to get sector times for an outlap. 
VehicleScoringInfoV2.mCurSector1, VehicleScoringInfoV2.mCurSector2 will 
read 0 until VehicleScoringInfoV2.mTotalLaps is > 0.

2. Therefore, the only way to get the out lap time (from logging starting to 
beginning the first lap) is to retrospectively use 
VehicleScoringInfoV2.mLapStartET on the beginning of the first full lap.

3. Similarly, it is not possible to get sector times for an outlap. In order
to do this I am using ScoringInfoV2.mCurrentET minus the ET when the first
lap began. This has a millisecond inaccuracy (as we wait for a new frame)
and so should *only* be used for the initial sector times and nothing more.

4. Exiting the pits, the current sector (VehicleScoringInfoV2.mCurrentSector)
is 1. It then becomes 2, 0 (third) and finally back to 1. Although rFactor 
displays a third sector time in-game, this is not exposed in the API.

5. Sector times are relative from the start of a lap (t > s2 > s1) and not
relative to each other (as shown in game).
*/
#include "LoggingPlugin.hpp"
#include "OpenMotorsport.hpp"
#include "ChannelDefinitions.hpp"
#include "Configuration.hpp"

#include <math.h>
#include <windows.h>
#include <sstream>

// Constants for the ScoringInfoV2.mGamePhases (see InternalsPlugin.hpp)
#define kGamePhaseNotEnteredGame 10
#define kGamePhaseBeforeSession 0
#define kGamePhaseReconnaissanceLaps 1
#define kGamePhaseGridWalkThrough 2
#define kGamePhaseFormationLap 3
#define kGamePhaseStartingLightCountdown 4
#define kGamePhaseGreenFlag 5
#define kGamePhaseFullCourseYellow 6
#define kGamePhaseSessionStopped 7
#define kGamePhaseSessionOver 8

// Constants for the TelemWheel.mTemperature array (see InternalsPlugin.hpp)
#define kWheelTemperatureLeft 0
#define kWheelTemperatureCenter 1
#define kWheelTemperatureRight 2

// Constants for the ScoringInfoV2.mCurrentSectors
#define kSectorsSector1 1
#define kSectorsSector2 2
#define kSectorsSector3 0

// Constant for the number of logged sectors
#define krFactorNumberOfSectors 2

// Constant for the rFactor Internals API version
#define krFactorInternalsVersion 3
#define krFactorPluginSubType "Internals"

// OpenMotorsport data source
#define kDataSource "rFactor"

#define kPluginName "rFactorOpenMotorsportPlugin"
#define kPluginDescription "Provides data logging to the OpenMotorsport format"
#define kPluginVersion 01
#define kPluginObjectCount 1

// Macros that aid the sampling code
#define RANGE_TO_PERCENT(x) 100.0f * x
#define BOOL_TO_FLOAT(x) (x) ? 1.0f : 0.0f
#define MPS_TO_KPH(x) x * 3.6f
#define SPEED_MPS(v) sqrtf( (v.x * v.x) + (v.y * v.y) + (v.z * v.z));
#define RAD_TO_DEG(x)  x * 57.296f;

/****************************************************************************/
/* LoggingPlugin definition.                                                */
/****************************************************************************/

// Static definitions
OpenMotorsport::Session* LoggingPlugin::Session;
std::deque<const TelemInfoV2>* LoggingPlugin::DataQueue;
int LoggingPlugin::mSamplingInterval;


// Plug-in lifecycle methods
void LoggingPlugin::Startup()
{
  LoggingPlugin::DataQueue = NULL;
  LoggingPlugin::Session = NULL;
  mConfiguration = new Configuration();
  mConfiguration->Read();
  // cache sample interval to stop a lookup for every thread loop
  LoggingPlugin::mSamplingInterval = 
        mConfiguration->GetInt(kConfigurationSampleInterval);
}

void LoggingPlugin::Destroy()
{
  Shutdown();
}

// Game lifecycle methods
void LoggingPlugin::EnterRealtime()
{
  mEnterPhase = kGamePhaseNotEnteredGame;
}

void LoggingPlugin::ExitRealtime()
{
  if(isCurrentlyLogging())
    stopLogging();
}

// Logging lifecycle methods
void LoggingPlugin::startLogging()
{
  mCurrentSector = kSectorsSector1;
  mSavedMetaData = false;
  mCurrentLapET = 0.0f;

  LoggingPlugin::DataQueue = new std::deque<const TelemInfoV2>();
  LoggingPlugin::CreateLoggingSession();

  DWORD dwThreadId;
  HANDLE thread = CreateThread( 
    NULL,                        // default security attributes
    0,                           // use default stack size  
    LoggingPlugin::LoggingThread, // thread function name
    NULL,                        // argument to thread function 
    0,                           // use default creation flags 
    &dwThreadId
    );
}

void LoggingPlugin::stopLogging()
{
  saveSession();
  LoggingPlugin::Session = NULL;
  LoggingPlugin::DataQueue = NULL;
  delete LoggingPlugin::Session;
  delete LoggingPlugin::DataQueue;
  mEnterPhase = kGamePhaseNotEnteredGame;
}

bool LoggingPlugin::isCurrentlyLogging()
{
  return !(LoggingPlugin::DataQueue == NULL);
}

void LoggingPlugin::saveSession()
{
  std::stringstream path;
  path << mConfiguration->GetString(kConfigurationOutputDirectory);
  CreateDirectory(path.str().c_str(), NULL);
  path << "\\";
  path << formatFileName(mConfiguration->GetString(kConfigurationFilename), 
    LoggingPlugin::Session);
  LoggingPlugin::Session->Write(path.str());
}

DWORD WINAPI LoggingPlugin::LoggingThread(LPVOID lpParam) 
{
  while(true) {
    // exit the thread if the DataQueue has been NULL'd
    if(LoggingPlugin::DataQueue == NULL) 
      return 0;

    if(!LoggingPlugin::DataQueue->empty())  {
      const TelemInfoV2 info = DataQueue->front();
      DataQueue->pop_front();

      LoggingPlugin::SampleBlock(info);
    }

    Sleep(LoggingPlugin::mSamplingInterval);
  }

  return 0;
} 

void LoggingPlugin::SampleBlock(const TelemInfoV2& info)
{
  // Compute some auxiliary info based on the above (vectors from ISI code)
  float speed = SPEED_MPS(info.mLocalVel);
  TelemVect3 forwardVector = { -info.mOriX.z, -info.mOriY.z, -info.mOriZ.z };
  TelemVect3 leftVector = { info.mOriX.x,  info.mOriY.x,  info.mOriZ.x };
  float pitch = atan2f( 
    forwardVector.y, 
    sqrtf( (forwardVector.x * forwardVector.x) + 
           (forwardVector.z * forwardVector.z) ) 
  );
  pitch = RAD_TO_DEG(pitch);

  float roll = atan2f( 
    leftVector.y, 
    sqrtf( (leftVector.x * leftVector.x) + 
           (leftVector.z * leftVector.z) ) 
  );
  roll = RAD_TO_DEG(roll);

  // Group: Position
  LoggingPlugin::Session->GetChannel(kChannelSpeed, kGroupPosition)
    .GetDataBuffer().Write(MPS_TO_KPH(speed));
  LoggingPlugin::Session->GetChannel(kChannelAccelerationX, kGroupPosition)
    .GetDataBuffer().Write(info.mLocalAccel.x);
  LoggingPlugin::Session->GetChannel(kChannelAccelerationY, kGroupPosition)
    .GetDataBuffer().Write(info.mLocalAccel.y);
  LoggingPlugin::Session->GetChannel(kChannelAccelerationZ, kGroupPosition)
    .GetDataBuffer().Write(info.mLocalAccel.z);
  LoggingPlugin::Session->GetChannel(kChannelPitch, kGroupPosition)
    .GetDataBuffer().Write(pitch);
  LoggingPlugin::Session->GetChannel(kChannelRoll, kGroupPosition).
    GetDataBuffer().Write(roll);

  // Group: Driver
  LoggingPlugin::Session->GetChannel(kChannelGear, kGroupDriver)
    .GetDataBuffer().Write(float(info.mGear));
  LoggingPlugin::Session->GetChannel(kChannelThrottle, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredThrottle));
  LoggingPlugin::Session->GetChannel(kChannelBrake, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredBrake));
  LoggingPlugin::Session->GetChannel(kChannelClutch, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredClutch));
  LoggingPlugin::Session->GetChannel(kChannelSteering, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredSteering));

  // Group: Engine
  LoggingPlugin::Session->GetChannel(kChannelRPM, kGroupEngine)
    .GetDataBuffer().Write(info.mEngineRPM);
  LoggingPlugin::Session->GetChannel(kChannelClutchRPM, kGroupEngine)
    .GetDataBuffer().Write(info.mClutchRPM);
  LoggingPlugin::Session->GetChannel(kChannelFuel, kGroupEngine)
    .GetDataBuffer().Write(info.mFuel);
  LoggingPlugin::Session->GetChannel(kChannelOverheating, kGroupEngine)
    .GetDataBuffer().Write(BOOL_TO_FLOAT(info.mOverheating));

  // Group: Wheels
  for( long i = 0; i < kNumberOfWheels; ++i ) {
    const TelemWheelV2 &wheel = info.mWheel[i];
    LoggingPlugin::Session->GetChannel(kChannelRotation, kWheels[i])
      .GetDataBuffer().Write(-wheel.mRotation);
    LoggingPlugin::Session->GetChannel(kChannelRotation, kWheels[i])
      .GetDataBuffer().Write(wheel.mSuspensionDeflection);
    LoggingPlugin::Session->GetChannel(kChannelRideHeight, kWheels[i])
      .GetDataBuffer().Write(wheel.mRideHeight);
    LoggingPlugin::Session->GetChannel(kChannelTireLoad, kWheels[i])
      .GetDataBuffer().Write(wheel.mTireLoad);
    LoggingPlugin::Session->GetChannel(kChannelLateralForce, kWheels[i])
      .GetDataBuffer().Write(wheel.mLateralForce);
    LoggingPlugin::Session->GetChannel(kChannelBrakeTemperature, kWheels[i])
      .GetDataBuffer().Write(wheel.mBrakeTemp);
    LoggingPlugin::Session->GetChannel(kChannelPressure, kWheels[i])
      .GetDataBuffer().Write(wheel.mPressure);
    LoggingPlugin::Session->GetChannel(kChannelTemperatureLeft, kWheels[i])
      .GetDataBuffer().Write(wheel.mTemperature[kWheelTemperatureLeft]);
    LoggingPlugin::Session->GetChannel(kChannelTemperatureCenter, kWheels[i])
      .GetDataBuffer().Write(wheel.mTemperature[kWheelTemperatureCenter]);
    LoggingPlugin::Session->GetChannel(kChannelTemperatureRight, kWheels[i])
      .GetDataBuffer().Write(wheel.mTemperature[kWheelTemperatureRight]);
  }
}

// Telemetry updates from InternalsPluginV3
void LoggingPlugin::UpdateTelemetry( const TelemInfoV2 &info )
{
  if(mEnterPhase == kGamePhaseNotEnteredGame) {
    mEnterPhase = mCurrentPhase;

    // Start logging immediately if we enter with 
    // kGamePhaseBeforeSession or kGamePhaseGreenFlag
    // otherwise, we will wait for a green flag and a lapStartET > 0
    switch(mEnterPhase) {
      case kGamePhaseBeforeSession:
      case kGamePhaseGreenFlag:
      case kGamePhaseFullCourseYellow:
        startLogging();
        break;
    }
  }

  // Check if we should start logging now
  if(!isCurrentlyLogging()) {
    if(mCurrentPhase >= kGamePhaseGreenFlag && info.mLapStartET > 0) {
      startLogging();        
    } else {
      return;
    }
  }

  // Put this telemetry update on the front of the queue.
  LoggingPlugin::DataQueue->push_front(info);
}

// Scoring updates from InternalsPluginV3
void LoggingPlugin::UpdateScoring( const ScoringInfoV2 &info )
{
  // It's possible to restart a race without leaving realtime mode so if we 
  // detect this, we need to manually stop/start logging
  if(info.mGamePhase < mCurrentPhase) {
    if(isCurrentlyLogging())
      stopLogging(); 
  }

  // Update current game phase
  mCurrentPhase = info.mGamePhase;

  // Sanity check so we don't end up crashing the game
  if(!isCurrentlyLogging()) 
    return;

  for(long i = 0; i < info.mNumVehicles; i++) {
    VehicleScoringInfoV2 &vinfo = info.mVehicle[i];
      
    if(vinfo.mIsPlayer) {
      if(!mSavedMetaData) {
        saveMetadata(info, vinfo);
        mSavedMetaData = !mSavedMetaData;
      }

      // We have advanced a sector so save the previous sector time
      if(vinfo.mSector != mCurrentSector) {
        mCurrentSector = vinfo.mSector;
        saveSectorTime(info, vinfo);
      }

      // We are only interested in this player.
      break;
    }
  }
}

void LoggingPlugin::saveSectorTime(const ScoringInfoV2& info,
                                   const VehicleScoringInfoV2& vinfo) 
{
  /*
  We effectively record two different sector times. If this is the first lap
  (outlap), we use an offset from the current ET and the lap start ET.
  Otherwise we use the (more accurate) sector times from VehicleScoringInfo.
  Additionally, we record the time for the sector *previous* to the current
  sector. So (there is no sector 3 time):
    
    - record sector 1 time when reach sector 2
    - record sector 2 time when reach sector 3
    - record lap time when reach sector 3
  */  
  switch(mCurrentSector) {
    case kSectorsSector1:
      if(vinfo.mLastLapTime > 0)
        LoggingPlugin::Session->
          AddRelativeMarker(vinfo.mLastLapTime - vinfo.mLastSector2);
      else
        LoggingPlugin::Session->AddMarker(vinfo.mLapStartET);
    break;
    
    case kSectorsSector2:
      if(vinfo.mCurSector1 > 0)
        LoggingPlugin::Session->AddRelativeMarker(vinfo.mCurSector1);
      else
        LoggingPlugin::Session->AddMarker(info.mCurrentET - vinfo.mLapStartET);
    break;

    case kSectorsSector3:
      if(vinfo.mCurSector2 > 0)
        LoggingPlugin::Session->
          AddRelativeMarker(vinfo.mCurSector2 - vinfo.mCurSector1);
      else
        LoggingPlugin::Session->AddMarker(info.mCurrentET - vinfo.mLapStartET);
    break;
  }
}

void LoggingPlugin::saveMetadata(const ScoringInfoV2& info,
                                 const VehicleScoringInfoV2& vinfo) {
  LoggingPlugin::Session->SetUser(vinfo.mDriverName);
  LoggingPlugin::Session->SetVehicle(vinfo.mVehicleName);
  LoggingPlugin::Session->SetTrack(info.mTrackName);
  LoggingPlugin::Session->SetDataSource(kDataSource);
  LoggingPlugin::Session->SetVehicleCategory(vinfo.mVehicleClass);
  LoggingPlugin::Session->SetNumberOfSectors(krFactorNumberOfSectors);
}

std::string LoggingPlugin::formatFileName(std::string format, 
                                          OpenMotorsport::Session* session)
{
  /*
  Create a filename according to a given format. Specifiers are:
    %Y - year (e.g. 2010)
    %M - month (e.g. 08)
    %D - day (e.g. 20)
    %H - hour (e.g 09)
    %M - minute (e.g. 20)
    %d - player name (e.g. Michael Schumacher)
    %t - track name (e.g. Toban Raceway)
    %c - vehicle name (e.g. rF3)
  */
  replace(format, "%Y", session->GetDate()->tm_year + 1900);
  replace(format, "%M", session->GetDate()->tm_mon);
  replace(format, "%D", session->GetDate()->tm_mday);
  replace(format, "%H", session->GetDate()->tm_hour);
  replace(format, "%M", session->GetDate()->tm_min);
  replace(format, "%c", session->GetVehicle());
  replace(format, "%t", session->GetTrack());
  replace(format, "%d", session->GetUser());
  return format;
}

// Long method to create channels. Ultimately this should be externalised into
// the configuration file OpenMotorsport.xml
void LoggingPlugin::CreateLoggingSession()
{
  LoggingPlugin::Session = new OpenMotorsport::Session();
  int channelID = 0;

  // Group: Position
  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelSpeed, 
      LoggingPlugin::mSamplingInterval, 
      kUnitsKPH, 
      kGroupPosition
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationX, 
      LoggingPlugin::mSamplingInterval, 
      kUnitsGee, 
      kGroupPosition
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationY, 
      LoggingPlugin::mSamplingInterval, 
      kUnitsGee, 
      kGroupPosition
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationZ,
      LoggingPlugin::mSamplingInterval, 
      kUnitsGee, 
      kGroupPosition
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelPitch,
      LoggingPlugin::mSamplingInterval, 
      kUnitsDegrees, 
      kGroupPosition
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelRoll,
      LoggingPlugin::mSamplingInterval, 
      kUnitsDegrees, 
      kGroupPosition
    )
  );

  // Group: Driver
  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelGear,
      LoggingPlugin::mSamplingInterval, 
      kUnitsGear, 
      kGroupDriver
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelThrottle,
      LoggingPlugin::mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelBrake,
      LoggingPlugin::mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelClutch,
      LoggingPlugin::mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelSteering,
      LoggingPlugin::mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  // Group: Engine
  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelRPM,
      LoggingPlugin::mSamplingInterval, 
      kUnitsRPM, 
      kGroupEngine
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelClutchRPM,
      LoggingPlugin::mSamplingInterval, 
      kUnitsRPM, 
      kGroupEngine
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelFuel,
      LoggingPlugin::mSamplingInterval, 
      kUnitsLitres, 
      kGroupEngine
    )
  );

  LoggingPlugin::Session->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelOverheating,
      LoggingPlugin::mSamplingInterval, 
      kUnitsBoolean, 
      kGroupEngine
    )
  );

  // Group: Wheels
  for( long i = 0; i < 4; ++i )
  {
    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelRotation,
        LoggingPlugin::mSamplingInterval, 
        kUnitsRadiansPerSecond, 
        kWheels[i]
      )
    ); 

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelSuspensionDeflection,
        LoggingPlugin::mSamplingInterval,
        kUnitsMeters, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelRideHeight,
        LoggingPlugin::mSamplingInterval,
        kUnitsMeters, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTireLoad,
        LoggingPlugin::mSamplingInterval,
        kUnitsNewtons, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelLateralForce,
        LoggingPlugin::mSamplingInterval,
        kUnitsNewtons, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelBrakeTemperature,
        LoggingPlugin::mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelPressure,
        LoggingPlugin::mSamplingInterval,
        kUnitsPascal, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureLeft,
        LoggingPlugin::mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureCenter,
        LoggingPlugin::mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    LoggingPlugin::Session->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureRight,
        LoggingPlugin::mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );
  }
}

// Performs an std::string find/replace with a template replacement.
template <class T>
void replace(std::string& str, std::string find, T replacement) {
  int pos = str.find(find);
  if(pos == std::string::npos) return;
  std::stringstream stream;
  stream << replacement;
  str.replace(pos, find.size(), stream.str());
}


/****************************************************************************/
/* Plug-in descriptors.                                                     */
/* Copyright (c) 1996-2007 Image Space Incorporated.                        */
/****************************************************************************/
InternalsPluginInfo g_PluginInfo;

// interface to plugin information
extern "C" __declspec(dllexport)
const char* __cdecl GetPluginName() 
{ 
  return kPluginName; 
}

extern "C" __declspec(dllexport)
unsigned __cdecl GetPluginVersion() 
{ 
  return kPluginVersion; 
}

extern "C" __declspec(dllexport)
unsigned __cdecl GetPluginObjectCount()
{ 
  return kPluginObjectCount; 
}

extern "C" __declspec(dllexport)
PluginObjectInfo* __cdecl GetPluginObjectInfo( const unsigned uIndex )
{
  switch(uIndex) {
    case 0:
      return  &g_PluginInfo;
    default:
      return 0;
  }
}

const char* InternalsPluginInfo::GetName() const 
{ 
  return kPluginName; 
}

const char* InternalsPluginInfo::GetFullName() const 
{ 
  return kPluginName; 
}

const char* InternalsPluginInfo::GetDesc() const 
{ 
  return kPluginDescription; 
}

const unsigned InternalsPluginInfo::GetType() const 
{ 
  return PO_INTERNALS; 
}

const char* InternalsPluginInfo::GetSubType() const 
{ 
  return krFactorPluginSubType; 
}

const unsigned InternalsPluginInfo::GetVersion() const
{ 
  return krFactorInternalsVersion; 
}

void* InternalsPluginInfo::Create() const 
{
  return new LoggingPlugin(); 
}

PluginObjectInfo *LoggingPlugin::GetInfo()
{
  return &g_PluginInfo;
}