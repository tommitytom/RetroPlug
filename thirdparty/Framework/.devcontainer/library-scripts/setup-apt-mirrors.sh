#!/bin/bash

STR=$(cat /etc/timezone)
SUB='Australia'

# Force using Australian apt mirror for dev environment for now
#if [[ "$STR" =~ .*"$SUB".* ]]; then
  #echo "Detected Australian Timezone"
  sed -i 's/archive.ubuntu.com/au.archive.ubuntu.com/' /etc/apt/sources.list
  sed -i 's/security.ubuntu.com/au.archive.ubuntu.com/' /etc/apt/sources.list
#fi
