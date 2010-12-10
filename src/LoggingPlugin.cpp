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
#include "LoggingPlugin.hpp"
#include "OpenMotorsport.hpp"
#include "ChannelDefinitions.hpp"
#include "Configuration.hpp"
#include "Utilities.hpp"

#include <math.h>
#include <windows.h>
#include <sstream>
#include <fstream>
#include <ctime>

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

// Constants for the ScoringInfo.mSession (see InternalsPlugin.hpp)
#define kSessionTesting 0
#define kSessionPractice 1
#define kSessionQualifying 5
#define kSessionWarmup 6
#define kSessionRace 7
static std::string kSessions[] = {
  "Testing", "Practice", "", "", "", "Qualifying", "Warmup", "Race"
};

// Constants for the TelemWheel.mTemperature array (see InternalsPlugin.hpp)
#define kWheelTemperatureLeft 0
#define kWheelTemperatureCenter 1
#define kWheelTemperatureRight 2

// Constants for the ScoringInfoV2.mCurrentSectors
#define kSectorsSector1 1
#define kSectorsSector2 2
#define kSectorsSector3 0

// Constant for an outlap
#define kOutlap 0

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

// Convert sectors to milliseconds
#define SEC_TO_MS(x) (int) (x * 1000)
#define MS_TO_SEC(x) (float) x / 1000
#define MSMS_TO_G(x) x * 0.101971621

// Copy an instance of TelemVect3 from src to dest
#define COPY_VECT3(src, dest) dest.x = src.x; dest.y = src.y; dest.z = src.z;

// Log file path
#define LOG_PATH "OpenMotorsport.log"

/****************************************************************************/
/* LoggingPlugin definition.                                                */
/****************************************************************************/

// Plug-in lifecycle methods
void LoggingPlugin::Startup()
{
  mSession = NULL;
  mIsLogging = false;
  mConfiguration = new Configuration();
  mConfiguration->Read();
  // cache sample interval to stop a lookup for every thread loop
  mSamplingInterval = 
        mConfiguration->GetInt(kConfigurationSampleInterval);
  mSamplingIntervalSeconds = MS_TO_SEC(mSamplingInterval);
  log("Startup");
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
void LoggingPlugin::startLogging(const TelemInfoV2 &info)
{
  mCurrentSector = kSectorsSector1;
  mSavedMetaData = false;
  mTotalElapsed = 0.0f;
  mFirstLapET = 0.0f;
  mEnterLapNumber = info.mLapNumber;
  mCurrentLapNumber = info.mLapNumber;
  mTimeSinceLastSample = 0.0f;
  mIsLogging = true;
  mHasPreviousPosition = false;
  mPreviousPosition.x = mPreviousPosition.y = mPreviousPosition.z = 0;
  mCumulativeDistance = 0.0f;

  LoggingPlugin::CreateLoggingSession();
  SampleBlock(info);

  log("Started logging");
}

void LoggingPlugin::stopLogging()
{
  mIsLogging = false;
  saveSession();
  mSession = NULL;
  delete mSession;
  mEnterPhase = kGamePhaseNotEnteredGame;
  
  log("Stopped logging");
}

bool LoggingPlugin::isCurrentlyLogging()
{
  return mIsLogging;
}

void LoggingPlugin::saveSession()
{
  if(mConfiguration->GetBool(kConfigurationRequireOneLap) &&
      (mCurrentLapNumber - mEnterLapNumber) < 1) {
    return;
  }

  std::stringstream path;
  path << mConfiguration->GetString(kConfigurationOutputDirectory);
  CreateDirectory(path.str().c_str(), NULL);
  path << "\\";
  path << formatFileName(mConfiguration->GetString(kConfigurationFilename), 
    mSession);

  try {
    mSession->Write(path.str());
  }
  catch (const char* e) {
    std::string message = 
      "Exception when attempting to write file: " + std::string(e);
    log(message, LOG_ERROR);
  }
}

void LoggingPlugin::SampleBlock(const TelemInfoV2& info)
{
  mCurrentLapNumber = info.mLapNumber;

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

  // Calculate cumulative distance (Cartesian distance)
  if(mHasPreviousPosition) {
    mCumulativeDistance += sqrtf(
      pow(mPreviousPosition.x - info.mPos.x, 2) +
      pow(mPreviousPosition.y - info.mPos.y, 2) +
      pow(mPreviousPosition.z - info.mPos.z, 2)
    );
  }
  COPY_VECT3(info.mPos, mPreviousPosition);
  mHasPreviousPosition = true;

  // Group: Acceleration
  mSession->GetChannel(kChannelAccelerationX, kGroupAcceleration)
    .GetDataBuffer().Write(MSMS_TO_G(info.mLocalAccel.x));
  mSession->GetChannel(kChannelAccelerationY, kGroupAcceleration)
    .GetDataBuffer().Write(MSMS_TO_G(info.mLocalAccel.y));
  mSession->GetChannel(kChannelAccelerationZ, kGroupAcceleration)
    .GetDataBuffer().Write(MSMS_TO_G(info.mLocalAccel.z));

  // Group: Position
  mSession->GetChannel(kChannelSpeed, kGroupPosition)
    .GetDataBuffer().Write(MPS_TO_KPH(speed));
  mSession->GetChannel(kChannelPitch, kGroupPosition)
    .GetDataBuffer().Write(pitch);
  mSession->GetChannel(kChannelRoll, kGroupPosition).
    GetDataBuffer().Write(roll);
  mSession->GetChannel(kChannelTime, kGroupPosition).
    GetDataBuffer().Write(SEC_TO_MS(mTotalElapsed));
  mSession->GetChannel(kChannelDistance, kGroupPosition).
    GetDataBuffer().Write(mCumulativeDistance);

  // Group: Driver
  mSession->GetChannel(kChannelGear, kGroupDriver)
    .GetDataBuffer().Write(float(info.mGear));
  mSession->GetChannel(kChannelThrottle, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredThrottle));
  mSession->GetChannel(kChannelBrake, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredBrake));
  mSession->GetChannel(kChannelClutch, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredClutch));
  mSession->GetChannel(kChannelSteering, kGroupDriver)
    .GetDataBuffer().Write(RANGE_TO_PERCENT(info.mUnfilteredSteering));

  // Group: Engine
  mSession->GetChannel(kChannelRPM, kGroupEngine)
    .GetDataBuffer().Write(info.mEngineRPM);
  mSession->GetChannel(kChannelClutchRPM, kGroupEngine)
    .GetDataBuffer().Write(info.mClutchRPM);
  mSession->GetChannel(kChannelFuel, kGroupEngine)
    .GetDataBuffer().Write(info.mFuel);
  mSession->GetChannel(kChannelOverheating, kGroupEngine)
    .GetDataBuffer().Write(BOOL_TO_FLOAT(info.mOverheating));

  // Group: Wheels
  for( long i = 0; i < kNumberOfWheels; ++i ) {
    const TelemWheelV2 &wheel = info.mWheel[i];
    mSession->GetChannel(kChannelSuspensionDeflection, kWheels[i])
      .GetDataBuffer().Write(wheel.mSuspensionDeflection);
    mSession->GetChannel(kChannelRotation, kWheels[i])
      .GetDataBuffer().Write(-wheel.mRotation);
    mSession->GetChannel(kChannelRideHeight, kWheels[i])
      .GetDataBuffer().Write(wheel.mRideHeight);
    mSession->GetChannel(kChannelTireLoad, kWheels[i])
      .GetDataBuffer().Write(wheel.mTireLoad);
    mSession->GetChannel(kChannelLateralForce, kWheels[i])
      .GetDataBuffer().Write(wheel.mLateralForce);
    mSession->GetChannel(kChannelBrakeTemperature, kWheels[i])
      .GetDataBuffer().Write(wheel.mBrakeTemp);
    mSession->GetChannel(kChannelPressure, kWheels[i])
      .GetDataBuffer().Write(wheel.mPressure);
    mSession->GetChannel(kChannelTemperatureLeft, kWheels[i])
      .GetDataBuffer().Write(wheel.mTemperature[kWheelTemperatureLeft]);
    mSession->GetChannel(kChannelTemperatureCenter, kWheels[i])
      .GetDataBuffer().Write(wheel.mTemperature[kWheelTemperatureCenter]);
    mSession->GetChannel(kChannelTemperatureRight, kWheels[i])
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
        startLogging(info);
        break;
    }
  }

  // Check if we should start logging now
  if(!isCurrentlyLogging()) {
    if(mCurrentPhase >= kGamePhaseGreenFlag && info.mLapStartET > 0) {
      startLogging(info);        
    } else {
      return;
    }
  }

  // We get the ET of the out lap by looking for when the lap number
  // increases. All other lap times after now handled by scoring.
  if(info.mLapNumber > mEnterLapNumber && mFirstLapET == 0.0f) {
    mFirstLapET = mTotalElapsed;
    mSession->AddMarker(SEC_TO_MS(mFirstLapET));
  }

  // Check if we should sample yet.
  if(mTimeSinceLastSample >= mSamplingIntervalSeconds) {
    SampleBlock(info);
    mTimeSinceLastSample = 0.0f;
  }

  mTotalElapsed += info.mDeltaTime;
  mTimeSinceLastSample += info.mDeltaTime;
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
        mSession->
          AddRelativeMarker(SEC_TO_MS((vinfo.mLastLapTime - vinfo.mLastSector2)));
    break;
    
    case kSectorsSector2:
      if(vinfo.mCurSector1 > 0)
        mSession->AddRelativeMarker(SEC_TO_MS(vinfo.mCurSector1));
      else
        mSession->AddMarker(
          SEC_TO_MS(mTotalElapsed)
        );
    break;

    case kSectorsSector3:
      if(vinfo.mCurSector2 > 0)
        mSession->
          AddRelativeMarker(SEC_TO_MS((vinfo.mCurSector2 - vinfo.mCurSector1)));
      else
        mSession->AddMarker(
          SEC_TO_MS(mTotalElapsed)
        );
    break;
  }
}

void LoggingPlugin::saveMetadata(const ScoringInfoV2& info,
                                 const VehicleScoringInfoV2& vinfo) 
{
  mSession->SetUser(vinfo.mDriverName);
  mSession->SetVehicle(vinfo.mVehicleName);
  mSession->SetTrack(info.mTrackName);
  mSession->SetDataSource(kDataSource);
  mSession->SetVehicleCategory(vinfo.mVehicleClass);
  mSession->SetNumberOfSectors(krFactorNumberOfSectors);
  mSession->SetComment(kSessions[info.mSession]);
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

void LoggingPlugin::log(std::string message, short level)
{
  std::ofstream out;
  out.open(LOG_PATH, std::ofstream::app | std::ofstream::out);
  if(out.is_open()) {
    time_t rawtime;
    time ( &rawtime );
    struct tm* date = localtime ( &rawtime );
    
    std::string prefix;
    switch(level) {
    case LOG_INFO:
      prefix = "INFO";
      break;
    case LOG_WARN:
      prefix = "WARN";
      break;
    case LOG_ERROR:
      prefix = "ERROR";
      break;
    }

    out << prefix << "(" << GetISO8601Date(date) << "): " << message << std::endl;
    out.close();
  }
}

// Long method to create channels. Ultimately this should be externalised into
// the configuration file OpenMotorsport.xml
void LoggingPlugin::CreateLoggingSession()
{
  mSession = new OpenMotorsport::Session();
  int channelID = 0;

  // Group: Acceleration
  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationX, 
      mSamplingInterval, 
      kUnitsGee, 
      kGroupAcceleration
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationY, 
      mSamplingInterval, 
      kUnitsGee, 
      kGroupAcceleration
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelAccelerationZ,
      mSamplingInterval, 
      kUnitsGee, 
      kGroupAcceleration
    )
  );

  // Group: Position
  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelSpeed, 
      mSamplingInterval, 
      kUnitsKPH, 
      kGroupPosition
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelPitch,
      mSamplingInterval, 
      kUnitsDegrees, 
      kGroupPosition
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelRoll,
      mSamplingInterval, 
      kUnitsDegrees, 
      kGroupPosition
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelTime,
      mSamplingInterval, 
      kUnitsMilliseconds, 
      kGroupPosition
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
    channelID++, 
    kChannelDistance,
    mSamplingInterval, 
    kUnitsMeters, 
    kGroupPosition
    )
  );

  // Group: Driver
  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelGear,
      mSamplingInterval, 
      kUnitsGear, 
      kGroupDriver
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelThrottle,
      mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelBrake,
      mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelClutch,
      mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelSteering,
      mSamplingInterval, 
      kUnitsPercent, 
      kGroupDriver
    )
  );

  // Group: Engine
  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelRPM,
      mSamplingInterval, 
      kUnitsRPM, 
      kGroupEngine
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelClutchRPM,
      mSamplingInterval, 
      kUnitsRPM, 
      kGroupEngine
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelFuel,
      mSamplingInterval, 
      kUnitsLitres, 
      kGroupEngine
    )
  );

  mSession->AddChannel(
    OpenMotorsport::Channel(
      channelID++, 
      kChannelOverheating,
      mSamplingInterval, 
      kUnitsBoolean, 
      kGroupEngine
    )
  );

  // Group: Wheels
  for( long i = 0; i < kNumberOfWheels; ++i )
  {
    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelRotation,
        mSamplingInterval, 
        kUnitsRadiansPerSecond, 
        kWheels[i]
      )
    ); 

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelSuspensionDeflection,
        mSamplingInterval,
        kUnitsMeters, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelRideHeight,
        mSamplingInterval,
        kUnitsMeters, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTireLoad,
        mSamplingInterval,
        kUnitsNewtons, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelLateralForce,
        mSamplingInterval,
        kUnitsNewtons, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelBrakeTemperature,
        mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelPressure,
        mSamplingInterval,
        kUnitsPascal, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureLeft,
        mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureCenter,
        mSamplingInterval,
        kUnitsCelcius, 
        kWheels[i]
      )
    );

    mSession->AddChannel(
      OpenMotorsport::Channel(
        channelID++, 
        kChannelTemperatureRight,
        mSamplingInterval,
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