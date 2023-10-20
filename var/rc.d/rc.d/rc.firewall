#!/bin/bash

# ---------------------------------------------------------------------------
# Slackware init script for iptables firewall:
# /etc/rc.d/rc.firewall
# Written by Eric Hameleers <alien@slackware.com> for the liveslak project.
# ---------------------------------------------------------------------------

# Specify path to the iptables binaries:
IPT_PATH="/usr/sbin"

# Save location for firewall rules:
[ ! -d /etc/firewall ] && mkdir /etc/firewall

# Is ipv6 supported on this computer?
if [ $(cat /sys/module/ipv6/parameters/disable) -eq 1 ]; then
  HAVE_IPV6=0
else
  HAVE_IPV6=1
fi

fwflush() {
  local IPT=${1:-iptables}
  # Accept all traffic first:
  ${IPT_PATH}/${IPT} -P INPUT ACCEPT
  ${IPT_PATH}/${IPT} -P FORWARD ACCEPT
  ${IPT_PATH}/${IPT} -P OUTPUT ACCEPT
  # Flush all iptables chains and rules:
  ${IPT_PATH}/${IPT} -F
  # Delete all iptables chains:
  ${IPT_PATH}/${IPT} -X
  # Flush all counters:
  ${IPT_PATH}/${IPT} -Z 
  # Flush/delete all nat and mangle rules:
  if [ "$IPT" != "ip6tables" ]; then
    ${IPT_PATH}/${IPT} -t nat -F
    ${IPT_PATH}/${IPT} -t nat -X
  fi
  ${IPT_PATH}/${IPT} -t mangle -F
  ${IPT_PATH}/${IPT} -t mangle -X
  ${IPT_PATH}/${IPT} -t raw -F
  ${IPT_PATH}//${IPT} -t raw -X
}

basic_protection() {
  # Basic measures to applied on first start:

  # Turn off packet forwarding in the kernel
  echo 0 > /proc/sys/net/ipv4/ip_forward
  # Enable TCP SYN Cookie Protection
  echo 1 > /proc/sys/net/ipv4/tcp_syncookies
  # Disable ICMP Redirect Acceptance
  echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects
  # Accept only from gateways in the default gateways list
  echo 1 > /proc/sys/net/ipv4/conf/all/secure_redirects
  # Do not send Redirect Messages
  echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects
  # Enable bad error message protection
  echo 1 > /proc/sys/net/ipv4/icmp_ignore_bogus_error_responses
  # Enable broadcast echo protection
  echo 1 > /proc/sys/net/ipv4/icmp_echo_ignore_broadcasts
  # Disable source-routed packets
  echo 0 > /proc/sys/net/ipv4/conf/all/accept_source_route
  # Do not log spoofed packets, source-routed packets, and redirect packets
  echo 0 > /proc/sys/net/ipv4/conf/all/log_martians
}

fw_start() {
  echo "Loading firewall rules..."
  # Apply basic protection in the kernel:
  basic_protection
  # Restore firewall rules:
  if [ -f /etc/firewall/ipv4 ]; then
    ${IPT_PATH}/iptables-restore  < /etc/firewall/ipv4
  else
    echo "** No saved ipv4 firewall rules found. Run 'myfwconf' first."
  fi
  if [ $HAVE_IPV6 -eq 1 ]; then
    if [ -f /etc/firewall/ipv6 ]; then
      ${IPT_PATH}/ip6tables-restore < /etc/firewall/ipv6
    else
      echo "** No saved ipv6 firewall rules found. Run 'myfwconf' first."
    fi
  fi
}

fw_reload() {
  fw_flush
  fw_start
}

fw_save() {
  # Save firewall rules:
  echo "Saving firewall rules..."
  ${IPT_PATH}/iptables -Ln 2>/dev/null
  [ $? -eq 0 ] && ${IPT_PATH}/iptables-save  > /etc/firewall/ipv4
  ${IPT_PATH}/ip6tables -Ln 2>/dev/null
  [ $? -eq 0 ] && ${IPT_PATH}/ip6tables-save > /etc/firewall/ipv6
}

fw_flush() {
  # Flush firewall rules, delete all custom chains and reset counters:
  # also resetting all policies to ACCEPT:
  echo "Flushing firewall rules..."
  fwflush iptables
  if [ $HAVE_IPV6 -eq 1 ]; then
    fwflush ip6tables
  fi
}

fw_status() {
  ${IPT_PATH}/iptables -L -n 2>/dev/null
  [ $? -ne 0 ] && echo "** No ipv4 support in the kernel!"
  ${IPT_PATH}/ip6tables -L -n 2>/dev/null
  [ $? -ne 0 ] && echo "** No ipv6 support in the kernel!"
}

case "$1" in
  start)
    fw_start
    ;;
  stop|flush)
    fw_flush
    ;;
  reload)
    fw_reload
    ;;
  save)
    fw_save
    ;;
  status)
    fw_status
    ;;
  *)
    echo "Usage: $0 start|stop|reload|save|flush|status"
    exit 1
    ;;
esac

exit 0

