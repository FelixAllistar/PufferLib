#ifndef SWAT_DEVICES_H
#define SWAT_DEVICES_H
#include "world.h"
#include "controller.h"
#define SWAT_MAX_DEVICES 8
typedef enum SwatDeviceKind { SWAT_CAMERA,SWAT_ROBOT,SWAT_BALL,SWAT_DRONE,SWAT_DEVICE_KINDS } SwatDeviceKind;
typedef struct SwatDevice {
    SwatTag tag;
    b3BodyId body;
    b3ShapeId shape;
    b3Pos position;
    b3Vec3 velocity;
    float yaw,pitch,health;
    int owner,age,battery_ticks,last_command_tick;
    SwatDeviceKind kind;
    bool active;
} SwatDevice;
struct SwatSim;
bool swat_device_deploy(struct SwatSim* sim,int actor,SwatDeviceKind kind);
bool swat_device_recover(struct SwatSim* sim,int actor);
void swat_devices_inputs(struct SwatSim* sim,SwatInput inputs[]);
void swat_devices_step(struct SwatSim* sim);
void swat_device_damage(struct SwatSim* sim,int index,float damage);
b3Pos swat_device_eye(const SwatDevice* device);
bool swat_feed_present(const struct SwatSim* sim,int unit);
const char* swat_device_name(SwatDeviceKind kind);
#endif
