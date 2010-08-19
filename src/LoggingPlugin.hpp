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
#ifndef _INTERNALS_EXAMPLE_H
#define _INTERNALS_EXAMPLE_H

#include "InternalsPlugin.hpp"
#include <deque>

namespace OpenMotorsport { class Session; }

/**
 * rFactor Plugin.
 */
class LoggingPlugin : public InternalsPluginV3
{
public:
  /**
   * Default constructor.
   */
  LoggingPlugin() {}

  /**
   * Deconstructor.
   */
  ~LoggingPlugin() {}

  /**
   * Plug-in lifecycle methods.
   */
  void Startup();
  void Destroy();

  /**
   * Game lifecycle methods. Realtime mode means driving.
   */
  void EnterRealtime();
  void ExitRealtime();

  /**
   * Receive telemetry updates.
   */
  bool WantsTelemetryUpdates() { return true; }

  /**
   * Called when a new telemetry update is available.
   *
   * @param info The telemetry information.
   */
  void UpdateTelemetry(const TelemInfoV2& info);

  /**
   * Receive scoring updates.
   */
  bool WantsScoringUpdates() { return true; }
  
  /**
   * Called when a new scoring update is available.
   *
   * @param info The scoring info.
   */
  void UpdateScoring(const ScoringInfoV2& info);

  /**
   * Derived from base class PluginObject.
   */
  PluginObjectInfo* GetInfo();
  unsigned GetPropertyCount() const { return 0; }
  PluginObjectProperty* GetProperty(const char*) { return 0; }
  PluginObjectProperty* GetProperty(const unsigned) { return 0; }

  /************** Static members for sampling. *******************/

  /**
   * A deque of telemetry blocks.
   */
  static std::deque<const TelemInfoV2>* DataQueue;
  
  /**
   * The current OpenMotorsport Session.
   */
  static OpenMotorsport::Session* Session;
  
  /**
   * The sampling thread.
   */
  static DWORD WINAPI LoggingThread(LPVOID lpParam);

  /**
   * Saves a block of telemetry into the current session.
   *
   * @param info The instance of TelemInfoV2 to sample.
   */
  static void SampleBlock(const TelemInfoV2& info);

  /**
   * Creates a new instance of OpenMotorsport::Session.
   */
  static void CreateLoggingSession();

private:
  // Maintaining the game state between the Scoring/Telemetry updates.
  float mCurrentLapET;
  signed char mCurrentSector;
  unsigned char mEnterPhase;
  unsigned char mCurrentPhase;
  bool mSavedMetaData;

  class Configuration* mConfiguration;
  static int mSamplingInterval;

private:
  void stopLogging();
  void startLogging();
  void saveSession();
  bool isCurrentlyLogging();
  void saveSectorTime(const ScoringInfoV2& info,
                      const VehicleScoringInfoV2& vinfo);
  void saveMetadata(const ScoringInfoV2& info,
                    const VehicleScoringInfoV2& vinfo);

  std::string LoggingPlugin::formatFileName(std::string format, 
                                            OpenMotorsport::Session* session);
};

/**
 * Performs an std::string find/replace with a template replacement.
 */
template <class T>
void replace(std::string& str, std::string find, T replacement);

/**
 * Standard plug-in descriptor.
 *
 * Copyright (c) 1996-2007 Image Space Incorporated.
 */
class InternalsPluginInfo : public PluginObjectInfo
{
public:
  /** 
   * Default constructor. 
   */
  InternalsPluginInfo() {}
  
  /**
   * Deconstructor.
   */
  ~InternalsPluginInfo() {}

  // Derived from base class PluginObjectInfo
  virtual const char* GetName() const;
  virtual const char* GetFullName() const;
  virtual const char* GetDesc() const;
  virtual const unsigned GetType() const;
  virtual const char* GetSubType() const;
  virtual const unsigned GetVersion() const;
  virtual void* Create() const;
};

#endif // _INTERNALS_EXAMPLE_H

