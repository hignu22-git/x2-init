#!/bin/sh
########-INStAlL-SCRiPt-#########
# MOD BY hignu22[ Andre Bobrovskiy]

if [ -e /etc/inittab ]
then
    echo "File /etc/inittab exists.... removing ..."
    sudo rm /etc/inittab
    sudo cp ../var/inittab 		/etc/ 
    sudo cp ../var/rc.d/rc.d    /etc/ 
else
    echo "File /etc/inittab does not exist"
    sudo cp ../var/inittab 		/etc/ 
    sudo cp ../var/rc.d/rc.d    /etc/ 
fi

echo " -< Installing x2-init as default... >-"
mv -v /sbin/init /sbin/alter-init
cp -v /sbin/x2-init /sbin/init

echo("@----@--@@@@------@@@-@----@-@@@-@---------");
echo("-@--@------@-------@--@@---@--@--@---------");
echo("--@@------@---###--@--@-@--@--@-@@@@-------");
echo("-@--@---@@---------@--@--@-@--@--@---@-----");
echo("@----@--@@@@------@@@-@---@@-@@@-@@@@@-----");

#######---END---#########
