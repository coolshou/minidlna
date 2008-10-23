#! /bin/sh
# $Id$
# miniupnp daemon
# http://miniupnp.free.fr or http://miniupnp.tuxfamily.org/
# (c) 2006-2007 Thomas Bernard
# This software is subject to the conditions detailed in the
# LICENCE file provided within the distribution

RM="rm -f"
CONFIGFILE="config.h"
CONFIGMACRO="__CONFIG_H__"

# version reported in XML descriptions
UPNP_VERSION=20070827
# Facility to syslog
LOG_MINIUPNPD="LOG_DAEMON"

# detecting the OS name and version
OS_NAME=`uname -s`
OS_VERSION=`uname -r`

# pfSense special case
if [ -f /etc/platform ]; then
	if [ `cat /etc/platform` = "pfSense" ]; then
		OS_NAME=pfSense
		OS_VERSION=`cat /etc/version`
	fi
fi

${RM} ${CONFIGFILE}

echo "/* MiniUPnP Project" >> ${CONFIGFILE}
echo " * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/" >> ${CONFIGFILE}
echo " * (c) 2006-2008 Thomas Bernard" >> ${CONFIGFILE}
echo " * generated by $0 on `date` */" >> ${CONFIGFILE}
echo "#ifndef $CONFIGMACRO" >> ${CONFIGFILE}
echo "#define $CONFIGMACRO" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}
echo "#define UPNP_VERSION	\"$UPNP_VERSION\"" >> ${CONFIGFILE}

# OS Specific stuff
case $OS_NAME in
	OpenBSD)
		MAJORVER=`echo $OS_VERSION | cut -d. -f1`
		MINORVER=`echo $OS_VERSION | cut -d. -f2`
		#echo "OpenBSD majorversion=$MAJORVER minorversion=$MINORVER"
		# rtableid was introduced in OpenBSD 4.0
		if [ $MAJORVER -ge 4 ]; then
			echo "#define PFRULE_HAS_RTABLEID" >> ${CONFIGFILE}
		fi
		# from the 3.8 version, packets and bytes counters are double : in/out
		if [ \( $MAJORVER -ge 4 \) -o \( $MAJORVER -eq 3 -a $MINORVER -ge 8 \) ]; then
			echo "#define PFRULE_INOUT_COUNTS" >> ${CONFIGFILE}
		fi
		echo "#define USE_PF 1" >> ${CONFIGFILE}
		OS_URL=http://www.openbsd.org/
		;;
	FreeBSD)
		VER=`grep '#define __FreeBSD_version' /usr/include/sys/param.h | awk '{print $3}'`
		if [ $VER -ge 700049 ]; then
			echo "#define PFRULE_INOUT_COUNTS" >> ${CONFIGFILE}
		fi
		if [ -f /usr/include/net/pfvar.h ] ; then
			echo "#define USE_PF 1" >> ${CONFIGFILE}
		else
			echo "#define USE_IPF 1" >> ${CONFIGFILE}
		fi
		OS_URL=http://www.freebsd.org/
		;;
	pfSense)
		# we need to detect if PFRULE_INOUT_COUNTS macro is needed
		echo "#define USE_PF 1" >> ${CONFIGFILE}
		OS_URL=http://www.pfsense.com/
		;;
	NetBSD)
		OS_URL=http://www.netbsd.org/
		if [ -f /usr/include/net/pfvar.h ] ; then
			echo "#define USE_PF 1" >> ${CONFIGFILE}
		else
			echo "#define USE_IPF 1" >> ${CONFIGFILE}
		fi
		;;
	SunOS)
		echo "#define USE_IPF 1" >> ${CONFIGFILE}
		echo "#define LOG_PERROR 0" >> ${CONFIGFILE}
		echo "#define SOLARIS_KSTATS 1" >> ${CONFIGFILE}
		echo "typedef uint64_t u_int64_t;" >> ${CONFIGFILE}
		echo "typedef uint32_t u_int32_t;" >> ${CONFIGFILE}
		echo "typedef uint16_t u_int16_t;" >> ${CONFIGFILE}
		echo "typedef uint8_t u_int8_t;" >> ${CONFIGFILE}
		OS_URL=http://www.sun.com/solaris/
		;;
	Linux)
		OS_URL=http://www.kernel.org/
		KERNVERA=`echo $OS_VERSION | awk -F. '{print $1}'`
		KERNVERB=`echo $OS_VERSION | awk -F. '{print $2}'`
		KERNVERC=`echo $OS_VERSION | awk -F. '{print $3}'`
		KERNVERD=`echo $OS_VERSION | awk -F. '{print $4}'`
		#echo "$KERNVERA.$KERNVERB.$KERNVERC.$KERNVERD"
		# Debian GNU/Linux special case
		if [ -f /etc/debian_version ]; then
			OS_NAME=Debian
			OS_VERSION=`cat /etc/debian_version`
			OS_URL=http://www.debian.org/
		fi
		# use lsb_release (Linux Standard Base) when available
		LSB_RELEASE=`which lsb_release`
		if [ 0 -eq $? ]; then
			OS_NAME=`${LSB_RELEASE} -i -s`
			OS_VERSION=`${LSB_RELEASE} -r -s`
		fi
		echo "#define USE_NETFILTER 1" >> ${CONFIGFILE}
		;;
	*)
		echo "Unknown OS : $OS_NAME"
		echo "Please contact the author at http://miniupnp.free.fr/"
		exit 1
		;;
esac

echo "#define OS_NAME		\"$OS_NAME\"" >> ${CONFIGFILE}
echo "#define OS_VERSION	\"$OS_NAME/$OS_VERSION\"" >> ${CONFIGFILE}
echo "#define OS_URL		\"${OS_URL}\"" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* syslog facility to be used by miniupnpd */" >> ${CONFIGFILE}
echo "#define LOG_MINIUPNPD		 ${LOG_MINIUPNPD}" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Uncomment the following line to allow miniupnpd to be" >> ${CONFIGFILE}
echo " * controlled by miniupnpdctl */" >> ${CONFIGFILE}
echo "/*#define USE_MINIUPNPDCTL*/" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Comment the following line to disable NAT-PMP operations */" >> ${CONFIGFILE}
echo "#define ENABLE_NATPMP" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Uncomment the following line to enable generation of" >> ${CONFIGFILE}
echo " * filter rules with pf */" >> ${CONFIGFILE}
echo "/*#define PF_ENABLE_FILTER_RULES*/">> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Uncomment the following line to enable caching of results of" >> ${CONFIGFILE}
echo " * the getifstats() function */" >> ${CONFIGFILE}
echo "/*#define ENABLE_GETIFSTATS_CACHING*/" >> ${CONFIGFILE}
echo "/* The cache duration is indicated in seconds */" >> ${CONFIGFILE}
echo "#define GETIFSTATS_CACHING_DURATION 2" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Uncomment the following line to enable multiple external ip support */" >> ${CONFIGFILE}
echo "/* note : Thas is EXPERIMENTAL, do not use that unless you know perfectly what you are doing */" >> ${CONFIGFILE}
echo "/*#define MULTIPLE_EXTERNAL_IP*/" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Comment the following line to use home made daemonize() func instead" >> ${CONFIGFILE}
echo " * of BSD daemon() */" >> ${CONFIGFILE}
echo "#define USE_DAEMON" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Uncomment the following line to enable lease file support */" >> ${CONFIGFILE}
echo "/*#define ENABLE_LEASEFILE*/" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Define one or none of the two following macros in order to make some" >> ${CONFIGFILE}
echo " * clients happy. It will change the XML Root Description of the IGD." >> ${CONFIGFILE}
echo " * Enabling the Layer3Forwarding Service seems to be the more compatible" >> ${CONFIGFILE}
echo " * option. */" >> ${CONFIGFILE}
echo "/*#define HAS_DUMMY_SERVICE*/" >> ${CONFIGFILE}
echo "#define ENABLE_L3F_SERVICE" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "/* Experimental UPnP Events support. */" >> ${CONFIGFILE}
echo "/*#define ENABLE_EVENTS*/" >> ${CONFIGFILE}
echo "" >> ${CONFIGFILE}

echo "#endif" >> ${CONFIGFILE}

exit 0
