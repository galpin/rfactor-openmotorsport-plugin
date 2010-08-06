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
#pragma once
#ifndef OPENMOTORSPORT_HPP
#define OPENMOTORSPORT_HPP

#include <string>
#include <unordered_map>
#include <vector>

#define kChannelVariableSampleInterval -1
#define kChannelNoUnits ""
#define kChannelNoGroup ""
#define kSessionNoVehicleCategory ""
#define kSessionNoUser "No User"
#define kSessionNoVehicleName "No Vehicle"
#define kSessionNoTrackName "No Track"
#define kSessionNoDataSource ""
#define kSessionNoSectors -1

class TiXmlElement;

namespace OpenMotorsport 
{
  /**
   * DataBuffer represents a basic data buffer used to write data samples
   * from a channel. The data is currently stored internally in-memory.
   */
  class DataBuffer 
  {
  public:
    /**
     * Default constructor.
     */
    DataBuffer();
    
    /**
     * Deconstructor.
     */
    virtual ~DataBuffer();

    /**
     * Writes a given value to the end of this data buffer.
     *
     * @param value A given float value.
     */
    void Write(float value);
    
    /**
     * @return Gets the total number of samples in this data buffer.
     */
    int GetLength();

    /**
     * @return Gets the total size of this data buffer (expressed in bytes).
     */
    int GetSize();

    /**
     * @return Gets the contents of this data buffer. The memory returned will
     * be GetSize() bytes in length.
     */
    float* GetBytes();

  private:
    typedef std::vector<float> DataBufferList;
    DataBufferList mData;
    DataBufferList mTimes;
  };

  /**
   * This class represents an OpenMotorsport channel. It contains a mandatory
   * id and name together with an optional sample interval (in milliseconds),
   * unit abbreviations and group name.
   */
  class Channel
  {
  public:
    /**
     * Default constructor. See alternative constructor.
     */
    Channel() {}

    /**
     * Desconstructor.
     */
    virtual ~Channel();

    /**
     * Constructs a new instance of Channel.
     * 
     * @param id A channel identifier. This must be unique within a of Session.
     * @param name The name of this channel.
     * @param sampleInterval The sample interval (in milliseconds). To indicate
     *   a variable sampling use the default value kChannelVariableSampleInterval.
     * @param units The units abbreivation (see the OpenMotorsport specification
     *   for a list of common abbreivations for units. Use the default value of
     *   kChannelNoUnits to indicate no units are given.
     * @param group The group name for this channel. Use the default value of
     *   kChannelNoGroup to indicate that the channel has no group.
     */
	  Channel(int id, const std::string name, 
      long sampleInterval = kChannelVariableSampleInterval,
      const std::string units = kChannelNoUnits,
      const std::string group = kChannelNoGroup);

    /**
     * @return The channel identifier.
     */
    const int GetId() const { return mId; }

    /**
     * @return The channel name.
     */
    const std::string& GetName() const { return mName; }
    
    /**
     * @return The channel group or kChannelNoGroup if it wasn't specified.
     */
    const std::string& GetGroup() const { return mGroup; }

    /**
     * @return The channel units abbreivation or kChannelNoUnits if they were not specified.
     */
    const std::string& GetUnits() const { return mUnits; }
    
    /**
     * @return The channel sampling interval (in milliseconds). Or, if this
     * is a variable sample interval, kChannelVariableSampleInterval.
     */
    const long GetSampleInterval() const { return mSampleInterval; }

    /**
     * @return Gets an instance of DataBuffer for this channel.
     */
    OpenMotorsport::DataBuffer& GetDataBuffer() { return mDataBuffer; }
  private:
	  int mId;
	  std::string mName;
	  std::string mGroup;
    std::string mUnits;
	  long mSampleInterval;
    DataBuffer mDataBuffer;
  };
 
  /**
   * This class represents an OpenMotorsport session. A instance of Session is
   * the centre of all reading/writing and manages associated metadata and channels.
   *
   * It is currently an incomplete implementation of the OpenMotorsport format
   * and does currently support reading existing files.
   */
  class Session
  {
  public:
    /**
     * Default constructor.
     */
    Session();

    /**
     * Deconstructor.
     */
    virtual ~Session();
    
    /**
     * Adds an instance of Channel to this session.
     *
     * @param channel An instance of Channel.
     */
    void AddChannel(Channel& channel);
    
    /**
     * Adds a new marker to this channel.
     *
     * @param The marker time in milliseconds from the start of the Session.
     */
    void AddMarker(float marker);

    /**
     * Adds a new marker to this channel using a time relative to the lap of a
     * lap. In this case, the actual marker recorded (that is always relative
     * to the start of the session) is the given marker plus the last recorded
     * marker.
     *
     * @param The marker time in milliseconds relative to the start of the lap.
     */
    void AddRelativeMarker(float marker);

    /**
     * Write this session to an OpenMotorsport file at the given path.
     *
     * @param filePath The filepath to write to. Include the filename and extension.
     */
    void Write(const std::string& filePath);

    /**
     * Get a channel by name and group.
     * 
     * @param The channel name.
     * @param The group of this channel.
     * @throws Exception if this channel could not be found.
     */
    Channel& GetChannel(const std::string& channelName, const std::string& group);

    /**
     * @param The number of sectors for this session. To indicate no sectors
     *   and no laps, use kSessionNoSectors.
     */
    void SetNumberOfSectors(short numSectors) { mNumSectors = numSectors; }
    
    /**
     * @return The number of sectors for this session or kSessionNoSectors.
     */
    short GetNumberOfSectors() const { return mNumSectors; }

    /**
     * @param fullName The full name of the user. Ideally, this should be a
     *  full name (firstname surname) but is not enforced.
     */
    void SetUser(const std::string fullName) { mFullName = fullName; }
    
    /**
     * @return Gets the name of the user.
     */
    const std::string& GetUser() const { return mFullName; }
    
    /**
     * @param vehicleName The name of the vehicle.
     */
    void SetVehicle(const std::string vehicleName) { mVehicleName = vehicleName; }
    
    /**
     * @return The name of the vehicle.
     */
    const std::string& GetVehicle() const { return mVehicleName; }

    /**
     * @param vehicleCategory The vehicle category.
     */
    void SetVehicleCategory(const std::string vehicleCatgeory) { mVehicleCategory = vehicleCatgeory; }
    
    /**
     * @return The name of the vehicle.
     */
    const std::string& GetVehicleCategory() const { return mVehicleCategory; }

    /**
     * @param trackName The name of the track.
     */
    void SetTrack(const std::string trackName) { mTrackName = trackName; }
    
    /**
     * @return The name of the track.
     */
    const std::string& GetTrack() const { return mTrackName; }

    /**
     * @param dataSource A description of the data source of this Session.
     */
    void SetDataSource(const std::string dataSource) { mDataSource = dataSource; }
    
    /**
     * @return A description of the data source of this Session.
     */
    const std::string& GetDataSource() const { return mDataSource; }

    /**
     * @return The date of this session as an ISO-8601 compatiable string.
     */
    const std::string GetISO8601Date() const;

    /**
     * @return The date of this session.
     */
    const struct tm* GetDate() const { return mDate; }

  private:
    void _createChannelXmlNode(const OpenMotorsport::Channel& channel, TiXmlElement* parent) const;
    TiXmlElement* Session::_createGroupXmlNode(const std::string& name, TiXmlElement* parent) const;
    std::string _writeMetaXml();
  
  private:
    typedef std::tr1::unordered_map<std::string, OpenMotorsport::Channel> ChannelsMap;
    typedef std::vector<float> MarkersList;
    
    ChannelsMap mChannels;
    MarkersList mMarkers;

    short mNumSectors;
    std::string mFullName;
    std::string mVehicleName;
    std::string mVehicleCategory;
    std::string mTrackName;
    std::string mDataSource;
    struct tm* mDate;
  };
}

#endif /* OPENMOTORSPORT_HPP */