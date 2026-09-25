// Pure C implementation of the pd64 FPS character controller.
// Ported from box3d samples/sample_character.cpp (RigidbodyCharacter), which
// credits s&box's PlayerController.
//
// Coordinate convention: ALL traces are FEET-space — pd_char_trace_body
// anchors its hull bottom to the trace origin. Body transform origin is the
// CENTER of the capsule, so converting between the two spaces happens only
// where we read/write b3Body transform/velocity.
//
// Build with -DPD_STEP_DEBUG=1 to trace TryStep decisions to stdout.

#include "character.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef PD_PI
#define PD_PI 3.14159265358979323846f
#endif

#ifndef PD_STEP_DEBUG
#define PD_STEP_DEBUG 0
#endif

static const float PD_SRC = 0.0254f; // 1 Source unit in meters

// ---------------------------------------------------------------------------
// tiny b3Vec3 helpers (we compile as C11 — no operator overloads)
// ---------------------------------------------------------------------------
static inline b3Vec3 v3( float x, float y, float z ) { b3Vec3 r = { x, y, z }; return r; }
static inline b3Vec3 v_add( b3Vec3 a, b3Vec3 b ) { return v3( a.x + b.x, a.y + b.y, a.z + b.z ); }
static inline b3Vec3 v_sub( b3Vec3 a, b3Vec3 b ) { return v3( a.x - b.x, a.y - b.y, a.z - b.z ); }
static inline b3Vec3 v_scale( b3Vec3 a, float s ) { return v3( a.x * s, a.y * s, a.z * s ); }

static inline b3Pos p_offset( b3Pos p, b3Vec3 v ) { return b3OffsetPos( p, v ); }

// ---------------------------------------------------------------------------
// TraceBody — closest-hit shape cast with own-shape ignore list
// ---------------------------------------------------------------------------

typedef struct CastContext
{
	b3ShapeId ignoreShapes[16];
	int ignoreCount;
	float closestFraction;
	b3Vec3 closestNormal;
	b3Pos closestPoint;
	b3ShapeId closestShape;
	bool hit;
	bool startedSolid;
} CastContext;

static float CastResultFcn( b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
							uint64_t userMaterialId, int triangleIndex, int childIndex,
							void* context )
{
	(void)userMaterialId;
	(void)triangleIndex;
	(void)childIndex;
	CastContext* ctx = (CastContext*)context;

	for ( int i = 0; i < ctx->ignoreCount; ++i )
	{
		if ( B3_ID_EQUALS( shapeId, ctx->ignoreShapes[i] ) )
		{
			return -1.0f;
		}
	}

	if ( fraction == 0.0f )
	{
		ctx->startedSolid = true;
		return -1.0f;
	}

	if ( fraction < ctx->closestFraction )
	{
		ctx->closestFraction = fraction;
		ctx->closestNormal = normal;
		ctx->closestPoint = point;
		ctx->closestShape = shapeId;
		ctx->hit = true;
	}

	return ctx->closestFraction;
}

pd_trace_result pd_char_trace_body( const PDCharacter* c, b3Pos from, b3Pos to,
									float radiusScale, float heightScale )
{
	pd_trace_result result;
	memset( &result, 0, sizeof( result ) );
	result.endPosition = to;
	result.normal = b3Vec3_axisY;
	result.point = to;
	result.fraction = 1.0f;

	b3Vec3 translation = b3SubPos( to, from );
	float translationLen = b3Length( translation );
	if ( translationLen < 1e-6f )
	{
		return result;
	}

	// Hull is BOTTOM-ANCHORED to the cast origin (feet at y=0)
	float halfW = c->bodyRadius * 0.5f * radiusScale;
	float halfH = c->totalHeight * heightScale * 0.5f;
	float halfD = c->bodyRadius * 0.5f * radiusScale;
	float boxCenterY = c->totalHeight * heightScale * 0.5f;

	b3Vec3 points[8];
	for ( int i = 0; i < 8; ++i )
	{
		float sx = ( i & 1 ) ? halfW : -halfW;
		float sy = ( i & 2 ) ? halfH : -halfH;
		float sz = ( i & 4 ) ? halfD : -halfD;
		points[i] = v3( sx, boxCenterY + sy, sz );
	}

	b3ShapeProxy proxy;
	proxy.points = points;
	proxy.count = 8;
	proxy.radius = 0.0f;

	CastContext ctx;
	memset( &ctx, 0, sizeof( ctx ) );
	for ( int i = 0; i < c->ownShapeCount && i < 16; ++i )
	{
		ctx.ignoreShapes[i] = c->ownShapes[i];
	}
	ctx.ignoreCount = c->ownShapeCount;
	ctx.closestFraction = 1.0f;

	b3QueryFilter filter = b3DefaultQueryFilter();
	b3World_CastShape( c->world, from, &proxy, translation, filter, CastResultFcn, &ctx );

	result.startedSolid = ctx.startedSolid;
	result.shapeId = b3_nullShapeId;
	if ( ctx.hit )
	{
		result.hit = true;
		result.fraction = ctx.closestFraction;
		result.normal = ctx.closestNormal;
		result.point = ctx.closestPoint;
		result.shapeId = ctx.closestShape;
		result.endPosition = p_offset( from, v_scale( translation, ctx.closestFraction ) );
	}
	return result;
}

// ---------------------------------------------------------------------------
// Ground categorization / snapping
// ---------------------------------------------------------------------------

static bool IsStandableSurface( const PDCharacter* c, b3Vec3 normal )
{
	float maxSlopeCos = cosf( c->maxSlopeAngleDeg * PD_PI / 180.0f );
	return b3Dot( normal, b3Vec3_axisY ) >= maxSlopeCos;
}

b3Pos pd_char_feet_position( const PDCharacter* c )
{
	b3Pos pos = b3Body_GetPosition( c->body );
	return p_offset( pos, v3( 0.0f, -c->totalHeight * 0.5f, 0.0f ) );
}

b3Pos pd_char_eye_position( const PDCharacter* c )
{
	// 8u below the crown of the current stance (standing: 64u above feet)
	return p_offset( pd_char_feet_position( c ),
					 v3( 0.0f, c->totalHeight - 8.0f * PD_SRC, 0.0f ) );
}

static void SetGroundState( PDCharacter* c, bool onGround, b3Vec3 normal )
{
	c->onGround = onGround;
	c->groundNormal = normal;
	if ( !onGround )
	{
		c->groundVelocity = v3( 0.0f, 0.0f, 0.0f );
	}
}

// Short downward box cast at the feet, shrinking radius when started solid or
// standing on something too steep.
static void CategorizeGround( PDCharacter* c )
{
	b3Pos feet = pd_char_feet_position( c );
	b3Pos from = p_offset( feet, v3( 0.0f, 4.0f * PD_SRC, 0.0f ) );
	b3Pos to = p_offset( feet, v3( 0.0f, -2.0f * PD_SRC, 0.0f ) );

	float radiusScale = 1.0f;
	pd_trace_result tr = pd_char_trace_body( c, from, to, radiusScale, 0.5f );

	while ( tr.startedSolid || ( tr.hit && !IsStandableSurface( c, tr.normal ) ) )
	{
		radiusScale -= 0.1f;
		if ( radiusScale < 0.7f )
		{
			SetGroundState( c, false, b3Vec3_axisY );
			return;
		}
		tr = pd_char_trace_body( c, from, to, radiusScale, 0.5f );
	}

	if ( !tr.startedSolid && tr.hit && IsStandableSurface( c, tr.normal ) &&
		 c->jumpCooldown <= 0.0f )
	{
		SetGroundState( c, true, tr.normal );
	}
	else
	{
		SetGroundState( c, false, b3Vec3_axisY );
	}
}

// Stick to ground: snap down onto surfaces when walking off small ledges /
// descending slopes so we don't go airborne every step.
static void Reground( PDCharacter* c, float stepSize )
{
	if ( !c->onGround )
	{
		return;
	}

	b3Pos pos = b3Body_GetPosition( c->body );
	b3Pos feet = pd_char_feet_position( c );
	b3Pos from = p_offset( feet, v3( 0.0f, 0.05f, 0.0f ) );
	b3Pos to = p_offset( feet, v3( 0.0f, -stepSize, 0.0f ) );

	float radiusScale = 1.0f;
	pd_trace_result tr = pd_char_trace_body( c, from, to, radiusScale, 0.5f );

	while ( tr.startedSolid )
	{
		radiusScale -= 0.1f;
		if ( radiusScale < 0.7f )
		{
			return;
		}
		tr = pd_char_trace_body( c, from, to, radiusScale, 0.5f );
	}

	if ( tr.hit )
	{
		// Place FEET on the surface (+1cm skin); transform origin is center
		b3Pos targetPos =
			p_offset( tr.endPosition, v3( 0.0f, c->totalHeight * 0.5f + 0.01f, 0.0f ) );
		float deltaY = targetPos.y - pos.y;

		b3Quat rot = b3Body_GetRotation( c->body );
		b3Body_SetTransform( c->body, targetPos, rot );

		if ( deltaY > 0.01f )
		{
			// Snapped upward: kill vertical velocity so we don't bounce
			b3Vec3 vel = b3Body_GetLinearVelocity( c->body );
			vel.y = 0.0f;
			b3Body_SetLinearVelocity( c->body, vel );
		}
	}
}

// Shape casts report "started solid" when the hull begins within
// ~1.25*B3_LINEAR_SLOP of any surface. s&box handles this by shrinking the
// cast radius and retrying; this wrapper applies that policy uniformly.
static pd_trace_result TraceShrunk( const PDCharacter* c, b3Pos from, b3Pos to,
									float heightScale )
{
	float radiusScale = 1.0f;
	pd_trace_result tr = pd_char_trace_body( c, from, to, radiusScale, heightScale );
	while ( tr.startedSolid && radiusScale > 0.6f )
	{
		radiusScale -= 0.1f;
		tr = pd_char_trace_body( c, from, to, radiusScale, heightScale );
	}
	return tr;
}

// ---------------------------------------------------------------------------
// Stair step-up: forward / up / across / down trace phases (all feet-space)
// ---------------------------------------------------------------------------
static bool TryStep( PDCharacter* c, float maxStepHeight )
{
	if ( !c->onGround )
	{
		return false;
	}

	b3Pos feet = pd_char_feet_position( c );
	b3Vec3 vel = b3Body_GetLinearVelocity( c->body );

	b3Vec3 hVel = v3( vel.x, 0.0f, vel.z );
	float hSpeed = b3Length( hVel );
	if ( hSpeed < 0.01f )
	{
		return false;
	}
	b3Vec3 moveDir = v_scale( hVel, 1.0f / hSpeed );

#if PD_STEP_DEBUG
	printf( "[step] feet=(%.3f, %.3f) hspeed=%.2f\n", (double)feet.x, (double)feet.y,
			(double)hSpeed );
#endif

	// Phase 1 — FORWARD: one frame of movement + radius, starting behind skin
	float forwardDist = hSpeed * ( 1.0f / 60.0f ) + c->bodyRadius;
	b3Pos forwardFrom = p_offset( feet, v_scale( moveDir, -c->skin ) );
	b3Pos forwardTo = p_offset( feet, v_scale( moveDir, forwardDist ) );

	pd_trace_result trForward = TraceShrunk( c, forwardFrom, forwardTo, 1.0f );

	if ( !trForward.hit )
	{
#if PD_STEP_DEBUG
		printf( "[step] P1: nothing ahead\n" );
#endif
		return false;
	}

#if PD_STEP_DEBUG
	printf( "[step] P1 hit frac=%.3f end=(%.3f, %.3f) shape=%u (own: %u,%u)\n",
			(double)trForward.fraction, (double)trForward.endPosition.x,
			(double)trForward.endPosition.y, trForward.shapeId.index1, c->ownShapes[0].index1,
			c->ownShapes[1].index1 );
#endif

	b3Pos hitPos = trForward.endPosition;

	// Phase 2 — UP
	b3Pos upFrom = hitPos;
	b3Pos upTo = p_offset( hitPos, v3( 0.0f, maxStepHeight, 0.0f ) );
	pd_trace_result trUp = TraceShrunk( c, upFrom, upTo, 1.0f );

	if ( trUp.startedSolid )
	{
#if PD_STEP_DEBUG
		printf( "[step] P2 startedSolid\n" );
#endif
		return false;
	}

	b3Pos topPos = trUp.hit ? trUp.endPosition : upTo;
	float upDistance = topPos.y - upFrom.y;
	if ( upDistance < 0.005f )
	{
#if PD_STEP_DEBUG
		printf( "[step] P2 too tight (%.4f)\n", (double)upDistance );
#endif
		return false;
	}

	// Phase 3 — ACROSS
	float acrossDist = forwardDist * ( 1.0f - trForward.fraction ) + c->bodyRadius * 0.5f;
	b3Pos acrossFrom = topPos;
	b3Pos acrossTo = p_offset( topPos, v_scale( moveDir, acrossDist ) );
	pd_trace_result trAcross = TraceShrunk( c, acrossFrom, acrossTo, 1.0f );

	if ( trAcross.startedSolid )
	{
#if PD_STEP_DEBUG
		printf( "[step] P3 startedSolid\n" );
#endif
		return false;
	}

	b3Pos acrossPos = trAcross.hit ? trAcross.endPosition : acrossTo;

	// Phase 4 — DOWN
	b3Pos downFrom = acrossPos;
	b3Pos downTo = p_offset( acrossPos, v3( 0.0f, -maxStepHeight, 0.0f ) );
	pd_trace_result trDown = TraceShrunk( c, downFrom, downTo, 1.0f );

	if ( !trDown.hit || !IsStandableSurface( c, trDown.normal ) )
	{
#if PD_STEP_DEBUG
		printf( "[step] P4 fail hit=%d normal=(%.2f, %.2f, %.2f)\n", trDown.hit,
				(double)trDown.normal.x, (double)trDown.normal.y, (double)trDown.normal.z );
#endif
		return false;
	}

	float stepHeight = trDown.endPosition.y - feet.y;
	if ( stepHeight < 0.01f )
	{
#if PD_STEP_DEBUG
		printf( "[step] lateral, not a step (%.4f)\n", (double)stepHeight );
#endif
		return false;
	}

#if PD_STEP_DEBUG
	printf( "[step] STEP UP %.3fm -> land y=%.3f\n", (double)stepHeight,
			(double)trDown.endPosition.y );
#endif

	// Teleport so FEET rest on the landing spot (+1cm); transform origin is center
	b3Pos stepPos =
		p_offset( trDown.endPosition, v3( 0.0f, c->totalHeight * 0.5f + 0.01f, 0.0f ) );
	b3Quat rot = b3Body_GetRotation( c->body );
	b3Body_SetTransform( c->body, stepPos, rot );

	b3Vec3 newVel = b3Body_GetLinearVelocity( c->body );
	newVel.x *= 0.9f;
	newVel.y = 0.0f;
	newVel.z *= 0.9f;
	b3Body_SetLinearVelocity( c->body, newVel );

	c->stepPosition = stepPos;
	return true;
}

static void RestoreStep( PDCharacter* c )
{
	if ( !c->didStep )
	{
		return;
	}
	b3Quat rot = b3Body_GetRotation( c->body );
	b3Body_SetTransform( c->body, c->stepPosition, rot );
	c->didStep = false;
}

// ---------------------------------------------------------------------------
// Velocity model (s&box MoveMode.Walk)
// ---------------------------------------------------------------------------

static b3Vec3 AddClamped( b3Vec3 current, b3Vec3 add, float maxAddLength )
{
	float addLen = b3Length( add );
	if ( addLen > maxAddLength && addLen > 0.0f )
	{
		add = v_scale( add, maxAddLength / addLen );
	}
	return v_add( current, add );
}

static void UpdateMassCenter( PDCharacter* c, float wishSpeed )
{
	b3MassData massData = b3Body_GetMassData( c->body );
	float halfHeight = c->totalHeight * 0.5f;

	if ( c->onGround )
	{
		float centerOffset = b3ClampFloat( wishSpeed, 0.0f, halfHeight );
		massData.center = v3( 0.0f, centerOffset - halfHeight, 0.0f );
	}
	else
	{
		massData.center = v3( 0.0f, 0.0f, 0.0f );
	}

	b3Body_SetMassData( c->body, massData );
}

static void UpdateBody( PDCharacter* c, b3Vec3 wishVelocity )
{
	float wishLen = b3Length( wishVelocity );
	b3Vec3 vel = b3Body_GetLinearVelocity( c->body );
	float velLen = b3Length( vel );

	// Feet friction: brake when wish speed is low relative to current speed
	float feetFriction = 0.0f;
	if ( c->onGround )
	{
		bool wantsBrakes = wishLen < ( 5.0f * PD_SRC ) || wishLen < velLen * 0.9f;
		if ( wantsBrakes )
		{
			feetFriction = 1.0f + 100.0f * c->brakePower * c->surfaceFriction;
		}
	}
	b3Shape_SetFriction( c->feetBoxId, feetFriction );

	UpdateMassCenter( c, wishLen );

	// Gravity only when it can actually act (moving or airborne)
	bool wantsGravity = false;
	if ( !c->onGround )
		wantsGravity = true;
	if ( velLen > ( 1.0f * PD_SRC ) )
		wantsGravity = true;
	if ( b3Length( c->groundVelocity ) > ( 1.0f * PD_SRC ) )
		wantsGravity = true;
	b3Body_SetGravityScale( c->body, wantsGravity ? ( c->gravity / 10.0f ) : 0.0f );

	// Damping acts as the brake when fully stopped on ground
	bool wantsDamping =
		c->onGround && wishLen < ( 1.0f * PD_SRC ) && b3Length( c->groundVelocity ) < ( 1.0f * PD_SRC );
	b3Body_SetLinearDamping( c->body, wantsDamping ? 10.0f * c->brakePower : c->airFriction );
}

static void AddVelocity( PDCharacter* c, b3Vec3 wishVelocity )
{
	b3Vec3 wish = v3( wishVelocity.x, 0.0f, wishVelocity.z );
	float wishLen = b3Length( wish );
	if ( wishLen < 0.001f )
	{
		return;
	}

	float groundFrictionFactor = 0.25f + c->surfaceFriction * 10.0f;
	b3Vec3 vel = b3Body_GetLinearVelocity( c->body );
	float savedY = vel.y;

	b3Vec3 velocity = v_sub( vel, c->groundVelocity );
	float speed = b3Length( velocity );
	float maxSpeed = b3MaxFloat( wishLen, speed );

	if ( c->onGround )
	{
		float amount = 1.0f * groundFrictionFactor;
		velocity = AddClamped( velocity, v_scale( wish, amount ), wishLen * amount );
	}
	else
	{
		float amount = 0.05f;
		velocity = AddClamped( velocity, v_scale( wish, amount ), wishLen );
	}

	float newSpeed = b3Length( velocity );
	if ( newSpeed > maxSpeed && newSpeed > 0.0f )
	{
		velocity = v_scale( velocity, maxSpeed / newSpeed );
	}

	velocity = v_add( velocity, c->groundVelocity );
	if ( c->onGround )
	{
		velocity.y = savedY;
	}

	b3Body_SetLinearVelocity( c->body, velocity );
}

// ---------------------------------------------------------------------------
// Stance (crouch) machinery
//
// Both shapes are rebuilt on stance change with densities recomputed so total
// mass stays constant (volume shrinks -> density rises). Feet box keeps the
// bottom half of the stance height, capsule the top half — same 50/50 split
// as the s&box original.
// ---------------------------------------------------------------------------

static void pd_char_apply_stance( PDCharacter* c )
{
	float h = c->totalHeight;
	float feetH = h * 0.5f;
	float halfW = c->bodyRadius * 0.5f;

	// --- feet box (bottom half, bottom-anchored) ---
	float fhx = halfW, fhy = feetH * 0.5f, fhz = halfW;
	b3Transform feetXf = { v3( 0.0f, -h * 0.5f + fhy, 0.0f ), b3Quat_identity };
	b3BoxHull feetHull = b3MakeTransformedBoxHull( fhx, fhy, fhz, feetXf );
	b3Shape_SetHull( c->feetBoxId, &feetHull.base );

	// --- capsule (top half) ---
	float capR = c->bodyRadius * 0.707f;
	float capBottom = -h * 0.5f + feetH + capR * 0.5f;
	float capTop = h * 0.5f - capR;
	if ( capTop <= capBottom )
	{
		capTop = capBottom + 0.01f;
	}
	b3Capsule capsule = { v3( 0.0f, capBottom, 0.0f ), v3( 0.0f, capTop, 0.0f ), capR };
	b3Shape_SetCapsule( c->capsuleId, &capsule );

	// Preserve mass split (40% feet / 60% capsule) despite volume change
	float feetVolume = 8.0f * fhx * fhy * fhz;
	float capLen = capTop - capBottom;
	float capVolume = PD_PI * capR * capR * ( capLen + 4.0f * capR / 3.0f );
	b3Shape_SetDensity( c->feetBoxId, ( c->mass * 0.4f ) / feetVolume, false );
	b3Shape_SetDensity( c->capsuleId, ( c->mass * 0.6f ) / capVolume, false );

	b3Body_ApplyMassFromShapes( c->body );
	UpdateMassCenter( c, b3Length( c->lastWishVelocity ) );

#if PD_STEP_DEBUG
	printf( "[stance] height=%.3f\n", (double)c->totalHeight );
#endif
}

void pd_char_set_crouch( PDCharacter* c, bool wantCrouch )
{
	c->crouchWish = wantCrouch;
	if ( wantCrouch == c->crouched )
	{
		return;
	}

	if ( wantCrouch )
	{
		float delta = c->standHeight - c->crouchHeight;
		c->totalHeight = c->crouchHeight;
		c->crouched = true;
		pd_char_apply_stance( c );

		// Keep feet planted: shrink happens around center, so drop the
		// transform origin by half the height difference.
		b3Pos pos = b3Body_GetPosition( c->body );
		b3Body_SetTransform( c->body, p_offset( pos, v3( 0.0f, -delta * 0.5f, 0.0f ) ),
							 b3Body_GetRotation( c->body ) );
		return;
	}

	// Stand up only with head clearance: sweep standing hull from current
	// pose upward through the height difference.
	float delta = c->standHeight - c->totalHeight;
	b3Pos feet = pd_char_feet_position( c );
	b3Pos from = p_offset( feet, v3( 0.0f, 0.02f, 0.0f ) );
	b3Pos to = p_offset( from, v3( 0.0f, delta + 0.02f, 0.0f ) );

	pd_trace_result tr = TraceShrunk( c, from, to, 1.0f );
	if ( tr.startedSolid || tr.hit )
	{
		return; // blocked; stay crouched (wish stays pending, retried in pre_step)
	}

	c->totalHeight = c->standHeight;
	c->crouched = false;
	pd_char_apply_stance( c );

	// Lift body so feet stay planted (origin is center; center rises by half
	// the height delta, matching the crouch transition above).
	b3Pos pos = b3Body_GetPosition( c->body );
	b3Body_SetTransform( c->body, p_offset( pos, v3( 0.0f, delta * 0.5f, 0.0f ) ),
						 b3Body_GetRotation( c->body ) );
}

bool pd_char_is_crouched( const PDCharacter* c )
{
	return c->crouched;
}

// ---------------------------------------------------------------------------
// Public stepping API
// ---------------------------------------------------------------------------

void pd_char_pre_step( PDCharacter* c, float timeStep, b3Vec3 forward, b3Vec3 right,
					   b3Vec2 throttle )
{
	if ( c->jumpCooldown > 0.0f )
	{
		c->jumpCooldown -= timeStep;
	}

	// Pending stand-up request retried every tick until it fits
	if ( c->crouchWish == false && c->crouched )
	{
		pd_char_set_crouch( c, false );
	}

	float maxSpeed = c->sprint ? c->runSpeed : c->walkSpeed;
	if ( c->crouched )
	{
		maxSpeed = c->crouchSpeed;
	}
	b3Vec3 wishVelocity = v_add( v_scale( forward, maxSpeed * throttle.x ),
								 v_scale( right, maxSpeed * throttle.y ) );
	float wishSpeed = b3Length( wishVelocity );
	if ( wishSpeed > maxSpeed )
	{
		wishVelocity = v_scale( wishVelocity, maxSpeed / wishSpeed );
	}
	c->lastWishVelocity = wishVelocity;

	UpdateBody( c, wishVelocity );
	AddVelocity( c, wishVelocity );
	c->didStep = TryStep( c, c->stepUpHeight );
}

void pd_char_post_step( PDCharacter* c, float timeStep )
{
	(void)timeStep;
	RestoreStep( c );
	Reground( c, c->stepDownHeight );
	CategorizeGround( c );
}

void pd_char_jump( PDCharacter* c )
{
	if ( c->onGround && c->jumpCooldown <= 0.0f )
	{
		b3Vec3 velocity = b3Body_GetLinearVelocity( c->body );
		velocity.y = c->jumpSpeed;
		b3Body_SetLinearVelocity( c->body, velocity );
		c->onGround = false;
		c->jumpCooldown = c->jumpCooldownTime;
	}
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void pd_char_init( PDCharacter* c, b3WorldId worldId, b3Pos position )
{
	memset( c, 0, sizeof( *c ) );

	c->world = worldId;

	// dimensions: radius 16u, standing 72u, crouched 40u
	c->bodyRadius = 16.0f * PD_SRC;
	c->standHeight = 72.0f * PD_SRC;
	c->crouchHeight = 40.0f * PD_SRC;
	c->totalHeight = c->standHeight;
	c->feetHeight = c->standHeight * 0.5f;

	// s&box tuning defaults
	c->walkSpeed = 230.0f * PD_SRC;
	c->runSpeed = 350.0f * PD_SRC;
	c->crouchSpeed = 100.0f * PD_SRC;
	c->jumpSpeed = 300.0f * PD_SRC;
	c->maxSlopeAngleDeg = 45.0f;
	c->gravity = 15.0f;
	c->mass = 500.0f;
	c->jumpCooldownTime = 0.2f;
	c->stepUpHeight = 18.0f * PD_SRC;
	c->stepDownHeight = 18.0f * PD_SRC;
	c->skin = 0.095f * PD_SRC;
	c->brakePower = 0.2f;
	c->surfaceFriction = 0.6f;
	c->airFriction = 0.1f;

	c->onGround = false;
	c->sprint = false;
	c->crouchWish = false;
	c->crouched = false;
	c->jumpCooldown = 0.0f;
	c->groundNormal = b3Vec3_axisY;
	c->groundVelocity = v3( 0.0f, 0.0f, 0.0f );
	c->lastWishVelocity = v3( 0.0f, 0.0f, 0.0f );
	c->didStep = false;
	c->stepPosition = b3Pos_zero;

	// Dynamic body, rotation fully locked, no sleeping (players never sleep)
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.type = b3_dynamicBody;
	bodyDef.position = position;
	bodyDef.motionLocks.angularX = true;
	bodyDef.motionLocks.angularY = true;
	bodyDef.motionLocks.angularZ = true;
	bodyDef.enableSleep = false;
	bodyDef.enableContactRecycling = false;
	bodyDef.gravityScale = c->gravity / 10.0f;

	c->body = b3CreateBody( worldId, &bodyDef );

	// Placeholder shapes; pd_char_apply_stance sizes them + sets densities
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.baseMaterial.friction = 0.0f;
	shapeDef.baseMaterial.restitution = 0.0f;

	{
		float halfExtY = c->totalHeight * 0.25f;
		b3Transform feetTransform = {
			v3( 0.0f, -c->totalHeight * 0.5f + halfExtY, 0.0f ), b3Quat_identity };
		b3BoxHull feetBox = b3MakeTransformedBoxHull(
			c->bodyRadius * 0.5f, halfExtY, c->bodyRadius * 0.5f, feetTransform );
		c->feetBoxId = b3CreateHullShape( c->body, &shapeDef, &feetBox.base );
	}
	{
		float capR = c->bodyRadius * 0.707f;
		b3Capsule capsule = { v3( 0.0f, -0.1f, 0.0f ), v3( 0.0f, 0.1f, 0.0f ), capR };
		c->capsuleId = b3CreateCapsuleShape( c->body, &shapeDef, &capsule );
	}

	c->ownShapeCount = b3Body_GetShapes( c->body, c->ownShapes, 4 );

	pd_char_apply_stance( c ); // standing geometry + mass
	UpdateMassCenter( c, 0.0f );
}
