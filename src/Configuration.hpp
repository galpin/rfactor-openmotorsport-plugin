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
#ifndef CONFIGURATION_HPP
#define CONFIGURATION_HPP

#define kConfigurationSampleInterval "SamplingInterval"
#define kConfigurationOutputDirectory "OutputDirectory"
#define kConfigurationFilename "Filename"

#define kDefaultFilename "%Y%M%D%H%M_%d_%c_%t.om"
#define kDefaultSampleInterval "200"
#define kDefaultOutputDirectory ".\\UserData\\LOG\\OpenMotorsport\\"
#define kDefaultConfigurationFile "OpenMotorsport.xml"

#include <string>
#include <unordered_map>

/**
 * This is a very basic configuration class that reads from a simple
 * XML configuration file. See "OpenMotorsport.xml" for more details.
 *
 * Mandatory configuration options have default values thus the existence of
 * the XML file is not strictly necessary for a dependant to continue working.
 */
class Configuration
{
public:
  Configuration(void);
  ~Configuration(void);
  void Read(std::string filename = kDefaultConfigurationFile);

  const std::string& GetString(std::string key);
  const int GetInt(std::string key);
private:
  typedef std::tr1::unordered_map<std::string, std::string> ConfigurationMap;
  ConfigurationMap mConfiguration;
private:
  void Configuration::parseOptionNode(class TiXmlElement* element);
};


#endif /* CONFIGURATION_HPP */