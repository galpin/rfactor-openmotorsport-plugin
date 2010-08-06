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
#include "Configuration.hpp"
#include "tinyxml.h"

#include <sstream>

#define kConfigurationRootNode "configuration"
#define kConfigurationOptionNode "option"

Configuration::Configuration(void)
{
  // initial default values
  mConfiguration[kConfigurationSampleInterval] = kDefaultSampleInterval;
  mConfiguration[kConfigurationOutputDirectory] = kDefaultOutputDirectory;
  mConfiguration[kConfigurationFilename] = kDefaultFilename;
}

Configuration::~Configuration(void)
{}

const std::string& Configuration::GetString(std::string key)
{
  return mConfiguration[key];
}

const int Configuration::GetInt(std::string key)
{
  std::stringstream stream(mConfiguration[key]);
  int result;
  stream >> result;
  return result;
}

void Configuration::Read(std::string filename)
{
  TiXmlDocument doc(filename.c_str());
  if(doc.LoadFile()) {
    TiXmlElement* root = doc.RootElement();
    if (strcmp(root->Value(), kConfigurationRootNode) == 0) {
      for(TiXmlElement* node = root->FirstChildElement(); 
        node; node = node->NextSiblingElement()) {
        if (strcmp(node->Value(), kConfigurationOptionNode) == 0)
          parseOptionNode(node);
      }
    }
  }
}

void Configuration::parseOptionNode(TiXmlElement* element)
{
  mConfiguration[element->Attribute("key")] = element->Attribute("value");
}