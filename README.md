# Phusion Passenger: a fast and robust web server and application server for Ruby, Python and Node.js

<img src="http://blog.phusion.nl/wp-content/uploads/2012/07/Passenger_chair_256x256.jpg" width="160" height="160" alt="Phusion Passenger">

## cgroup + chroot support

This fork of (https://github.com/phusion/passenger)[Phusion Passenger] includes support for cgroup assignment and chroot in Apache (*nginx maybe later*).

### cgroups

Set a Cgroup directive in a VirtualHost container:

    Cgroup site12
    
All Passenger apps spawned from the web server will inherit this control group. It can also be used to enforce memory/cpu limits.    

### Jailing

Edit ```src/ruby_supportlib/phusion_passenger/constants.rb```. Change SITE_CHROOT_ENV to another environment var. By default ```SITE_ROOT``` is used. Constants.h will be automatically regenerated on next build. Chroot could also be adapted to a directive, but fits our usage. To populate a SITE_ROOT, add SetEnvIf in the VirtualHost container, e.g.

    SetEnvIf Request_Method ^.*$ SITE_ROOT=/home/mydomain.com
    
Note: you could use SetEnv, but SetEnv may be overwritten in a .htaccess file with another path.

## Further reading
 * [Phusion Passenger repository](https://github.com/phusion/passenger)
 * The `doc/` directory.
 * [Contributors Guide](https://github.com/phusion/passenger/blob/master/CONTRIBUTING.md)
 * https://www.phusionpassenger.com/support

## Legal

"Passenger", "Phusion Passenger" and "Union Station" are registered trademarks of Phusion Holding B.V.
