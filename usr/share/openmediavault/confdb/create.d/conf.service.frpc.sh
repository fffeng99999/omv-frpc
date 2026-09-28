#!/usr/bin/env dash
#
# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    fffeng99999
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

set -e

. /usr/share/openmediavault/scripts/helper-functions

########################################################################
# Update the configuration.
# <config>
#   <services>
#     <frpc>
#       <enable>0|1</enable>
#       <backend>docker|native</backend>
#       <dockerContainer>frpc</dockerContainer>
#       <dockerConfigPath>/etc/frp/frpc.toml</dockerConfigPath>
#       <dockerContainerConfigPath>/etc/frp/frpc.toml</dockerContainerConfigPath>
#       <serverAddr></serverAddr>
#       <serverPort>7000</serverPort>
#       <user></user>
#       <token></token>
#       <protocol>tcp</protocol>
#       <tlsEnable>1</tlsEnable>
#       <tlsServerName></tlsServerName>
#       <tcpMuxEnable>1</tcpMuxEnable>
#       <poolCount>0</poolCount>
#       <heartbeatInterval>30</heartbeatInterval>
#       <heartbeatTimeout>90</heartbeatTimeout>
#       <dialTimeout>10</dialTimeout>
#       <proxyUrl></proxyUrl>
#       <dnsServer></dnsServer>
#       <loginFailExit>1</loginFailExit>
#       <logLevel>info</logLevel>
#       <logMaxDays>3</logMaxDays>
#       <logPrintContent>0</logPrintContent>
#       <webEnable>0</webEnable>
#       <webAddr>127.0.0.1</webAddr>
#       <webPort>7400</webPort>
#       <webUser></webUser>
#       <webPassword></webPassword>
#       <rawConfig></rawConfig>
#       <proxies></proxies>
#     </frpc>
#   </services>
# </config>
########################################################################
if ! omv_config_exists "/config/services/frpc"; then
	omv_config_add_node "/config/services" "frpc"
	omv_config_add_key "/config/services/frpc" "enable" "0"
	omv_config_add_key "/config/services/frpc" "backend" "docker"
	omv_config_add_key "/config/services/frpc" "dockerContainer" "frpc"
	omv_config_add_key "/config/services/frpc" "dockerConfigPath" "/etc/frp/frpc.toml"
	omv_config_add_key "/config/services/frpc" "dockerContainerConfigPath" "/etc/frp/frpc.toml"
	omv_config_add_key "/config/services/frpc" "serverAddr" ""
	omv_config_add_key "/config/services/frpc" "serverPort" "7000"
	omv_config_add_key "/config/services/frpc" "user" ""
	omv_config_add_key "/config/services/frpc" "token" ""
	omv_config_add_key "/config/services/frpc" "protocol" "tcp"
	omv_config_add_key "/config/services/frpc" "tlsEnable" "1"
	omv_config_add_key "/config/services/frpc" "tlsServerName" ""
	omv_config_add_key "/config/services/frpc" "tcpMuxEnable" "1"
	omv_config_add_key "/config/services/frpc" "poolCount" "0"
	omv_config_add_key "/config/services/frpc" "heartbeatInterval" "30"
	omv_config_add_key "/config/services/frpc" "heartbeatTimeout" "90"
	omv_config_add_key "/config/services/frpc" "dialTimeout" "10"
	omv_config_add_key "/config/services/frpc" "proxyUrl" ""
	omv_config_add_key "/config/services/frpc" "dnsServer" ""
	omv_config_add_key "/config/services/frpc" "loginFailExit" "1"
	omv_config_add_key "/config/services/frpc" "logLevel" "info"
	omv_config_add_key "/config/services/frpc" "logMaxDays" "3"
	omv_config_add_key "/config/services/frpc" "logPrintContent" "0"
	omv_config_add_key "/config/services/frpc" "webEnable" "0"
	omv_config_add_key "/config/services/frpc" "webAddr" "127.0.0.1"
	omv_config_add_key "/config/services/frpc" "webPort" "7400"
	omv_config_add_key "/config/services/frpc" "webUser" ""
	omv_config_add_key "/config/services/frpc" "webPassword" ""
	# Array fields (meta/includes) and the <proxies> container are created
	# on demand; do not add empty <meta>/<includes> elements here, they
	# would be read back as an array containing one empty string.
	omv_config_add_key "/config/services/frpc" "rawConfig" ""
	omv_config_add_node "/config/services/frpc" "proxies"
fi

exit 0
