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
#include <windows.h>
#include <stdio.h>
#include <time.h> 

#include "OpenMotorsport.hpp"
#include "tinyxml.h"
#include "zip.h"

// xmlns namespace for meta.xml
#define kXmlBaseNamespace "http://66laps.org/ns/openmotorsport-1.0"

// Initial capacity for a data buffer (3000 samples is 10 minutes @ 5Hz)
#define kDataBufferInitialCapacity 3000

// Check for existance of a key in an std unsorted_map
#define MAP_HAS_KEY(map, key) !(map.find(key) == map.end())

namespace OpenMotorsport 
{
  Session::Session() :
    mNumSectors(kSessionNoSectors),
    mVehicleCategory(kSessionNoVehicleCategory),
    mFullName(kSessionNoUser),
    mTrackName(kSessionNoTrackName),
    mVehicleName(kSessionNoVehicleName),
    mDataSource(kSessionNoDataSource)
  {
    // Initialise default date
    time_t rawtime;
    time ( &rawtime );
    mDate = localtime ( &rawtime );
  }

  Session::~Session()
  {
  }

  void Session::Write(const std::string& fileName)
  {   
    int error;
    zipFile zf;
    
    zf = zipOpen(fileName.c_str(), APPEND_STATUS_CREATE);
    if(zf == NULL) {
      throw "Failed to open OpenMotorsport file writing.";
    }

    // write the meta.xml to the ZIP file
    std::string metaXml = _writeMetaXml();
    error = zipWriteNewFile(zf, "meta.xml", mDate,
        metaXml.c_str(), metaXml.size());
    if(error != ZIP_OK) {
      throw "Failed to write OpenMotorsport/meta.xml.";
    }

    // write channel data to ZIP file
    for(ChannelsMap::iterator it = this->mChannels.begin();
      it != this->mChannels.end(); ++it)
    {
      Channel& channel = it->second;
      
      char dataFileName[MAX_PATH];
      sprintf(dataFileName, "data/%d.bin", channel.GetId());

      error = zipWriteNewFile(zf, dataFileName, mDate,
        channel.GetDataBuffer().GetBytes(), channel.GetDataBuffer().GetSize());
      if(error != ZIP_OK) {
        throw "Failed to write channel data.";
      }
    }

    error = zipClose(zf,NULL);
    if (error != ZIP_OK) {
      throw "Failed to close OpenMotorsport file.";
    }
  }

  const std::string Session::GetISO8601Date() const
  {
    char* buf = new char[80]; // magic number, 80 is enough for an ISO-8601 date
    strftime(buf, 80, "%Y-%m-%dT%H:%M:%S", mDate);
    std::string str(buf);
    delete [] buf;
    return str;
  }

  void Session::AddChannel(Channel& channel)
  {
    std::string key = channel.GetName() + "/" + channel.GetGroup();
    mChannels.insert(ChannelsMap::value_type(key, channel));
  }

  void Session::AddMarker(int marker)
  { 
    mMarkers.push_back(marker); 
  }

  void Session::AddRelativeMarker(int marker)
  { 
    if(mMarkers.size() < 1)
      mMarkers.push_back(marker); // no need to adjust as there is no previous
    else
      mMarkers.push_back(mMarkers.back() + marker); 
  }

  Channel& Session::GetChannel(const std::string& channelName,
                               const std::string& group)
  {
    if(!mChannels.size()) throw "No channels.";
    std::string key = channelName + "/" + group;
    if(!MAP_HAS_KEY(mChannels, key)) throw "Channel does not exist.";
    return mChannels[key];
  }

  std::string Session::_writeMetaXml()
  {
    TiXmlDocument doc;
    TiXmlElement* node;
    TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
    doc.LinkEndChild(decl);

    TiXmlElement* root = new TiXmlElement("openmotorsport");
    root->SetAttribute("xmlns", kXmlBaseNamespace);
    doc.LinkEndChild(root);

    // write basic <metadata>
    TiXmlElement* metadata = new TiXmlElement("metadata");
    root->LinkEndChild(metadata);  

    node = new TiXmlElement("user");
    node->LinkEndChild(new TiXmlText(this->mFullName.c_str()));
    metadata->LinkEndChild(node);

    // write vehicle name (and optional category)
    TiXmlElement* vehicle = new TiXmlElement("vehicle");
    metadata->LinkEndChild(vehicle);
    node = new TiXmlElement("name");  
    node->LinkEndChild(new TiXmlText(this->mVehicleName.c_str()));
    vehicle->LinkEndChild(node);

    if(this->mVehicleCategory != kSessionNoVehicleCategory) {
      node = new TiXmlElement("category");
      node->LinkEndChild(new TiXmlText(this->mVehicleCategory.c_str()));
    }

    TiXmlElement* venue = new TiXmlElement("venue");
    metadata->LinkEndChild(venue);
    node = new TiXmlElement("name");
    node->LinkEndChild(new TiXmlText(this->mTrackName.c_str()));
    venue->LinkEndChild(node);

    node = new TiXmlElement("date");
    node->LinkEndChild(new TiXmlText(this->GetISO8601Date().c_str()));
    metadata->LinkEndChild(node);

    node = new TiXmlElement("datasource");
    node->LinkEndChild(new TiXmlText(this->mDataSource.c_str()));
    metadata->LinkEndChild(node);

    node = new TiXmlElement("comments");
    node->LinkEndChild(new TiXmlText(this->mComments.c_str()));
    metadata->LinkEndChild(node);

    // write <channels>
    TiXmlElement* channels = new TiXmlElement("channels");
    root->LinkEndChild(channels);

    // for the channel/group hierachy map group names with a corresponding node
    std::tr1::unordered_map<std::string, TiXmlElement*> groupNodes;

    for(ChannelsMap::iterator it = this->mChannels.begin();
      it != this->mChannels.end(); ++it)
    {
      Channel& channel = it->second;
      TiXmlElement* parent = channels;

      if(channel.GetGroup() != kChannelNoGroup) {
        // create a new group node if this key doe not already exist
        if(!MAP_HAS_KEY(groupNodes, channel.GetGroup())) {
          groupNodes[channel.GetGroup()] 
            = _createGroupXmlNode(channel.GetGroup(), channels);
        }
        parent = groupNodes[channel.GetGroup()];
      }
      _createChannelXmlNode(channel, parent);
    }
    
    // write <markers>
    TiXmlElement* markers = new TiXmlElement("markers");
    root->LinkEndChild(markers);
    if(this->mNumSectors != kSessionNoSectors)
      markers->SetAttribute("sectors", this->mNumSectors);

    for(MarkersList::iterator it = this->mMarkers.begin();
      it != this->mMarkers.end(); ++it)
    {
      node = new TiXmlElement("marker");
      node->SetDoubleAttribute("time", *it);
      markers->LinkEndChild(node);
    }
    
    TiXmlPrinter printer;
    printer.SetIndent("\t");
    doc.Accept( &printer );
    return std::string(printer.CStr());
  }

  TiXmlElement* Session::_createGroupXmlNode(const std::string& groupName,
                                             TiXmlElement* parent) const
  {
    TiXmlElement* node;
    TiXmlElement* name;

    node = new TiXmlElement("group");
    parent->LinkEndChild(node);
     
    name = new TiXmlElement("name");
    name->LinkEndChild(new TiXmlText(groupName.c_str()));
    node->LinkEndChild(name);

    return node;
  }

  void Session::_createChannelXmlNode(const OpenMotorsport::Channel& channel,
                                      TiXmlElement* parent) const
  {
    TiXmlElement* node;
    TiXmlElement* name;

    node = new TiXmlElement("channel");
    parent->LinkEndChild(node);
    node->SetAttribute("id", channel.GetId());
    if(channel.GetUnits() != kChannelNoUnits) 
      node->SetAttribute("units", channel.GetUnits().c_str());
    if(channel.GetSampleInterval() != kChannelVariableSampleInterval) 
      node->SetAttribute("interval", channel.GetSampleInterval());
     
    name = new TiXmlElement("name");
    name->LinkEndChild(new TiXmlText(channel.GetName().c_str()));
    node->LinkEndChild(name);
  }


  /****************************************************************************/
  /* Definition of OpenMotorsport::Channel. */
  /****************************************************************************/

  Channel::Channel(int id, const std::string name, long sampleInterval,
    const std::string units, const std::string group)
    : mId(id), mName(name), mSampleInterval(sampleInterval),
    mUnits(units), mGroup(group)
  {}

  Channel::~Channel() 
  {}

  /****************************************************************************/
  /* Definition of OpenMotorsport::DataBuffer. */
  /****************************************************************************/

  DataBuffer::DataBuffer()
  {
    mData.reserve(kDataBufferInitialCapacity);
  }

  DataBuffer::~DataBuffer()
  {}

  void DataBuffer::Write(float value)
  {
    mData.push_back(value);
  }

  int DataBuffer::GetLength()
  {
    return mData.size();
  }

  int DataBuffer::GetSize()
  {
    return mData.size() * sizeof(float);
  }

  float* DataBuffer::GetBytes()
  {
    float* data = new float[mData.size()];
    int i = 0;
    for(DataBufferList::iterator it = mData.begin(); it != mData.end(); ++it)
    {
      float value = *it;
      data[i++] = value;
    }
    return data;
  }
}
