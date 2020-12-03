if [ ${USER} == "ec2-user" ];then
	echo "We're on AWS"
	if [ -f aws_config.h ];then
		rm aws_config.h
	fi
	touch aws_config.h
	echo "#define AWS_HASHTABLE 1" | tee -a aws_config.h
	cd build
	cmake ..
	make
else
	echo "We're on yeti nodes"
	if [ -f aws_config.h ];then
                rm aws_config.h
        fi
	cd build
        cmake ..
        make
fi
