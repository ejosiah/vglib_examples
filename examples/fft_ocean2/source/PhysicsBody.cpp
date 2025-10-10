#include "PhysicsBody.hpp"

/*
====================================================
Bounds::operator =
====================================================
*/
const Bounds & Bounds::operator = ( const Bounds & rhs ) {
    mins = rhs.mins;
    maxs = rhs.maxs;
    return *this;
}

/*
====================================================
Bounds::DoesIntersect
====================================================
*/
bool Bounds::DoesIntersect( const Bounds & rhs ) const {
    if ( maxs.x < rhs.mins.x || maxs.y < rhs.mins.y || maxs.z < rhs.mins.z ) {
        return false;
    }
    if ( rhs.maxs.x < mins.x || rhs.maxs.y < mins.y || rhs.maxs.z < mins.z ) {
        return false;
    }
    return true;
}

/*
====================================================
Bounds::Expand
====================================================
*/
void Bounds::Expand( const glm::vec3 * pts, const int num ) {
    for ( int i = 0; i < num; i++ ) {
        Expand( pts[ i ] );
    }
}

/*
====================================================
Bounds::Expand
====================================================
*/
void Bounds::Expand( const glm::vec3 & rhs ) {
    if ( rhs.x < mins.x ) {
        mins.x = rhs.x;
    }
    if ( rhs.y < mins.y ) {
        mins.y = rhs.y;
    }
    if ( rhs.z < mins.z ) {
        mins.z = rhs.z;
    }

    if ( rhs.x > maxs.x ) {
        maxs.x = rhs.x;
    }
    if ( rhs.y > maxs.y ) {
        maxs.y = rhs.y;
    }
    if ( rhs.z > maxs.z ) {
        maxs.z = rhs.z;
    }
}

/*
====================================================
Bounds::Expand
====================================================
*/
void Bounds::Expand( const Bounds & rhs ) {
    Expand( rhs.mins );
    Expand( rhs.maxs );
}

/*
========================================================================================================

ShapeSphere

========================================================================================================
*/

/*
====================================================
ShapeSphere::Support
====================================================
*/
glm::vec3 ShapeSphere::Support( const glm::vec3 & dir, const glm::vec3 & pos, const glm::quat & orient, const float bias ) const {
    glm::vec3 supportPt;

    // TODO: Add code

    return supportPt;
}

/*
====================================================
ShapeSphere::InertiaTensor
====================================================
*/
glm::mat3 ShapeSphere::InertiaTensor() const {
    glm::mat3 tensor{2.0f * m_radius * m_radius / 5.0f};
    return tensor;
}

/*
====================================================
ShapeSphere::GetBounds
====================================================
*/
Bounds ShapeSphere::GetBounds( const glm::vec3 & pos, const glm::quat & orient ) const {
    Bounds tmp;
    tmp.mins = glm::vec3( -m_radius ) + pos;
    tmp.maxs = glm::vec3( m_radius ) + pos;
    return tmp;
}

/*
====================================================
ShapeSphere::GetBounds
====================================================
*/
Bounds ShapeSphere::GetBounds() const {
    Bounds tmp;
    tmp.mins = glm::vec3( -m_radius );
    tmp.maxs = glm::vec3( m_radius );
    return tmp;
}


/*
====================================================
PhysicsBody::GetCenterOfMassWorldSpace
====================================================
*/
glm::vec3 PhysicsBody::GetCenterOfMassWorldSpace() const {
    const glm::vec3 centerOfMass = m_shape->GetCenterOfMass();
    const glm::vec3 pos =   m_position + m_orientation * centerOfMass;
    return pos;
}

/*
====================================================
PhysicsBody::GetCenterOfMassModelSpace
====================================================
*/
glm::vec3 PhysicsBody::GetCenterOfMassModelSpace() const {
    const glm::vec3 centerOfMass = m_shape->GetCenterOfMass();
    return centerOfMass;
}

/*
====================================================
PhysicsBody::WorldSpaceToBodySpace
====================================================
*/
glm::vec3 PhysicsBody::WorldSpaceToBodySpace( const glm::vec3 & worldPt ) const {
    glm::vec3 tmp			= worldPt - GetCenterOfMassWorldSpace();
    glm::quat inverseOrient	= glm::inverse(m_orientation);
    glm::vec3 bodySpace		= inverseOrient * tmp;
    return bodySpace;
}

/*
====================================================
PhysicsBody::BodySpaceToWorldSpace
====================================================
*/
glm::vec3 PhysicsBody::BodySpaceToWorldSpace( const glm::vec3 & worldPt ) const {
    glm::vec3 worldSpace = GetCenterOfMassWorldSpace() + m_orientation * worldPt;
    return worldSpace;
}

/*
====================================================
PhysicsBody::GetInverseInertiaTensorBodySpace
====================================================
*/
glm::mat3 PhysicsBody::GetInverseInertiaTensorBodySpace() const {
    glm::mat3 inertiaTensor		= m_shape->InertiaTensor();
    glm::mat3 invInertiaTensor	= glm::inverse(inertiaTensor) * m_invMass;
    return invInertiaTensor;
}

/*
====================================================
PhysicsBody::GetInverseInertiaTensorWorldSpace
====================================================
*/
glm::mat3 PhysicsBody::GetInverseInertiaTensorWorldSpace() const {
    glm::mat3 inertiaTensor		= m_shape->InertiaTensor();
    glm::mat3 invInertiaTensor	= glm::inverse(inertiaTensor) * m_invMass;
    glm::mat3 orient					= glm::mat3(m_orientation);
    invInertiaTensor		= orient * invInertiaTensor * glm::transpose(orient);
    return invInertiaTensor;
}

/*
====================================================
PhysicsBody::ApplyImpulse
====================================================
*/
void PhysicsBody::ApplyImpulse( const glm::vec3 & impulsePoint, const glm::vec3 & impulse ) {
    if ( 0.0f == m_invMass ) {
        return;
    }

    // impulsePoint is the world space location of the application of the impulse
    // impulse is the world space direction and magnitude of the impulse
    ApplyImpulseLinear( impulse );

    glm::vec3 position = GetCenterOfMassWorldSpace();	// applying impulses must produce torques through the center of mass
    glm::vec3 r = impulsePoint - position;
    glm::vec3 dL = glm::cross(r, impulse );	// this is in world space
    ApplyImpulseAngular( dL );
}

/*
====================================================
PhysicsBody::ApplyImpulseLinear
====================================================
*/
void PhysicsBody::ApplyImpulseLinear( const glm::vec3 & impulse ) {
    if ( 0.0f == m_invMass ) {
        return;
    }

    // p = mv
    // dp = m dv = J
    // => dv = J / m
    m_linearVelocity += impulse * m_invMass;
}

/*
====================================================
PhysicsBody::ApplyImpulseAngular
====================================================
*/
void PhysicsBody::ApplyImpulseAngular( const glm::vec3 & impulse ) {
    if ( 0.0f == m_invMass ) {
        return;
    }

    // L = I w = r x p
    // dL = I dw = r x J 
    // => dw = I^-1 * ( r x J )
    m_angularVelocity += GetInverseInertiaTensorWorldSpace() * impulse;

    const float maxAngularSpeed = 30.0f; // 30 rad/s is fast enough for us. But feel free to adjust.
    if ( glm::dot(m_angularVelocity, m_angularVelocity) > maxAngularSpeed * maxAngularSpeed ) {
        glm::normalize(m_angularVelocity);
        m_angularVelocity *= maxAngularSpeed;
    }
}

/*
====================================================
PhysicsBody::Update
====================================================
*/
void PhysicsBody::Update( const float dt_sec ) {
    m_position += m_linearVelocity * dt_sec;

    // okay, we have an angular velocity around the center of mass, this needs to be
    // converted somehow to relative to model position.  This way we can properly update
    // the orientation of the model.
    glm::vec3 positionCM = GetCenterOfMassWorldSpace();
    glm::vec3 cmToPos = m_position - positionCM;

    // Total Torque is equal to external applied torques + internal torque (precession)
    // T = T_external + omega x I * omega
    // T_external = 0 because it was applied in the collision response function
    // T = Ia = w x I * w
    // a = I^-1 ( w x I * w )
    glm::mat3 orientation = glm::mat3(m_orientation);
    glm::mat3 inertiaTensor = orientation * m_shape->InertiaTensor() * glm::transpose(orientation);
    glm::vec3 alpha = glm::inverse(inertiaTensor) * glm::cross( m_angularVelocity, ( inertiaTensor * m_angularVelocity ) );
    m_angularVelocity += alpha * dt_sec;

    // Update orientation
    glm::vec3 dAngle = m_angularVelocity * dt_sec;
    glm::quat dq = glm::quat(glm::length(dAngle) , dAngle);
    m_orientation = dq * m_orientation;
    glm::normalize(m_orientation);

    // Now get the new model position
    m_position = positionCM + dq * cmToPos;
}