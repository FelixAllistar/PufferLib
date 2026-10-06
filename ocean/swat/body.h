// SWAT: Gold Element local controller fork. See PROVENANCE.md.
// pd64 FPS character controller — pure C port of box3d's RigidbodyCharacter
// sample (samples/sample_character.cpp), which is itself a port of
// s&box's PlayerController. No C++ anywhere: everything below goes through
// box3d's public C API.
//
// Design summary (see sample for full commentary):
//   - dynamic body, all rotation locked
//   - two shapes: low "feet" box (receives braking friction) +
//     frictionless body capsule (slides along walls)
//   - capped horizontal wish-velocity acceleration, sprint/walk speeds
//   - 4-phase trace step-up for stairs (forward/up/across/down)
//   - stick-to-ground snap (Reground) so ramps/stairs don't bounce
//   - ground categorization via box cast with radius shrinking,
//     standable-surface check by slope angle

#ifndef SWAT_BODY_H
#define SWAT_BODY_H

#include "box3d/box3d.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SwatTraceResult
{
	bool hit;
	bool startedSolid;
	float fraction;      // 1.0 = reached `to`
	b3Vec3 normal;
	b3Pos point;         // contact point
	b3Pos endPosition;   // from + fraction * (to - from)
	b3ShapeId shapeId;   // what was hit (invalid if !hit)
} SwatTraceResult;

typedef struct SwatBody
{
	// handles
	b3WorldId world;
	b3BodyId body;
	b3ShapeId feetBoxId;
	b3ShapeId capsuleId;
	b3ShapeId ownShapes[4];
	int ownShapeCount;

	// dimensions (meters) — s&box defaults: radius 16u, height 72u, 1u = 0.0254m
	float bodyRadius;
	float totalHeight;       // CURRENT stance height (== stand or crouch)
	float standHeight;       // 72u
	float crouchHeight;      // 40u
	float feetHeight;        // bottom fraction of current stance
	b3Vec3 upperOffset;     // swept tactical lean; feet remain under the pelvis

	// tuning (s&box PlayerController defaults, converted)
	float walkSpeed;         // 230 u/s
	float runSpeed;          // 350 u/s
	float crouchSpeed;       // 100 u/s
	float jumpSpeed;         // 300 u/s
	float maxSlopeAngleDeg;  // 45
	float gravity;           // 15 m/s^2 (s&box character gravity)
	float mass;              // 500 kg equivalent
	float stepUpHeight;      // 18 u
	float stepDownHeight;    // 18 u
	float skin;              // 0.095 u
	float brakePower;        // 0.2
	float surfaceFriction;   // 0.6
	float airFriction;       // 0.1
	float jumpCooldownTime;  // 0.2 s

	// state
	b3Vec3 groundNormal;
	b3Vec3 groundVelocity;
	float jumpCooldown;
	bool onGround;
	bool sprint;
	bool crouchWish;         // requested stance
	bool crouched;           // actual stance (may differ from wish when blocked)
	bool didStep;            // stepped this physics frame; restore after solve
	b3Pos stepPosition;
	b3Vec3 lastWishVelocity;
} SwatBody;

// Create body + shapes inside `world`. All tuning fields get s&box defaults;
// caller may override them AFTER init but BEFORE first pre_step (speeds etc).
void swat_body_init( SwatBody* c, b3WorldId worldId, b3Pos position );

// Request stance change. Crouch applies immediately; standing only succeeds
// if there is head clearance (checked with a trace), otherwise the request
// stays pending and is retried every pre_step until it fits.
void swat_body_set_crouch( SwatBody* c, bool wantCrouch );
bool swat_body_is_crouched( const SwatBody* c );

// Call before b3World_Step: applies wish velocity, updates friction/gravity
// damping, attempts stair step-up. forward/right must be horizontal unit
// vectors; throttle.x = +fwd/-back, throttle.y = +right/-left.
void swat_body_pre_step( SwatBody* c, float timeStep, b3Vec3 forward, b3Vec3 right,
					   b3Vec2 throttle );

// Call after b3World_Step: restores step pose, snaps to ground,
// re-categorizes ground contact.
void swat_body_post_step( SwatBody* c, float timeStep );

void swat_body_jump( SwatBody* c );

// Box-cast a scaled character hull from `from` to `to`, ignoring own shapes.
// radiusScale shrinks width, heightScale shrinks height (bottom anchored).
SwatTraceResult swat_body_trace_body( const SwatBody* c, b3Pos from, b3Pos to,
									float radiusScale, float heightScale );

b3Pos swat_body_feet_position( const SwatBody* c );
b3Pos swat_body_eye_position( const SwatBody* c ); // stance-dependent height

// Move the real upper-body capsule, sweeping its volume against the world.
// Returns the achieved offset. Rotation is locked, so offsets are world-space.
b3Vec3 swat_body_lean( SwatBody* c, b3Vec3 desiredOffset );
// Apply a validated authoritative pose to a client replica without running
// movement/clearance decisions again. Replica worlds never step game rules.
void swat_body_replica_pose( SwatBody* c, b3Pos position, b3Vec3 velocity,
                            bool crouched, bool grounded, b3Vec3 upperOffset );

#ifdef __cplusplus
}
#endif

#endif // SWAT_BODY_H
