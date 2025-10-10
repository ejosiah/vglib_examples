#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

struct Bounds {
    Bounds() { Clear(); }
    Bounds( const Bounds & rhs ) : mins( rhs.mins ), maxs( rhs.maxs ) {}
    const Bounds & operator = ( const Bounds & rhs );
    ~Bounds() {}

    void Clear() { mins = glm::vec3( 1e6 ); maxs = glm::vec3( -1e6 ); }
    bool DoesIntersect( const Bounds & rhs ) const;
    void Expand( const glm::vec3 * pts, const int num );
    void Expand( const glm::vec3 & rhs );
    void Expand( const Bounds & rhs );

    float WidthX() const { return maxs.x - mins.x; }
    float WidthY() const { return maxs.y - mins.y; }
    float WidthZ() const { return maxs.z - mins.z; }

    glm::vec3 mins;
    glm::vec3 maxs;
};

struct Shape {
    virtual glm::mat3 InertiaTensor() const = 0;

    virtual Bounds GetBounds( const glm::vec3 & pos, const glm::quat & orient ) const = 0;
    virtual Bounds GetBounds() const = 0;

    virtual glm::vec3 GetCenterOfMass() const { return m_centerOfMass; }

    enum shapeType_t {
        SHAPE_SPHERE,
        SHAPE_BOX,
        SHAPE_CONVEX,
    };
    virtual shapeType_t GetType() const = 0;

    virtual glm::vec3 Support( const glm::vec3 & dir, const glm::vec3 & pos, const glm::quat & orient, const float bias ) const = 0;

    virtual float FastestLinearSpeed( const glm::vec3 & angularVelocity, const glm::vec3 & dir ) const { return 0.0f; }

protected:
    glm::vec3 m_centerOfMass{};
};

class ShapeSphere : public Shape {
public:
    ShapeSphere(float radius) :m_radius(radius) {}

    glm::vec3 Support( const glm::vec3 & dir, const glm::vec3 & pos, const glm::quat & orient, const float bias ) const override;

    glm::mat3 InertiaTensor() const override;

    Bounds GetBounds( const glm::vec3 & pos, const glm::quat & orient ) const override;
    Bounds GetBounds() const override;

    shapeType_t GetType() const override { return SHAPE_SPHERE; }

private:
    const float m_radius;
};

/*
====================================================
ShapeBox
====================================================
*/
class ShapeBox : public Shape {
public:
    explicit ShapeBox( const glm::vec3 * pts, const int num ) {
        Build( pts, num );
    }
    void Build( const glm::vec3 * pts, const int num );

    glm::vec3 Support( const glm::vec3 & dir, const glm::vec3 & pos, const glm::quat & orient, const float bias ) const override;

    glm::mat3 InertiaTensor() const override;

    Bounds GetBounds( const glm::vec3 & pos, const glm::quat & orient ) const override;
    Bounds GetBounds() const override { return m_bounds; }

    float FastestLinearSpeed( const glm::vec3 & angularVelocity, const glm::vec3 & dir ) const override;

    shapeType_t GetType() const override { return SHAPE_BOX; }

private:
    std::vector<glm::vec3> m_points;
    Bounds m_bounds;
};

struct PhysicsBody {

    glm::vec3		m_position{};
    glm::quat		m_orientation{1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3		m_linearVelocity{};
    glm::vec3		m_angularVelocity{};
    std::shared_ptr<Shape>          m_shape{};

    float		m_invMass{0};
    float		m_elasticity{0};
    float		m_friction{1};

    glm::vec3 GetCenterOfMassWorldSpace() const;
    glm::vec3 GetCenterOfMassModelSpace() const;

    glm::vec3 WorldSpaceToBodySpace( const glm::vec3 & pt ) const;
    glm::vec3 BodySpaceToWorldSpace( const glm::vec3 & pt ) const;

    glm::mat3 GetInverseInertiaTensorBodySpace() const;
    glm::mat3 GetInverseInertiaTensorWorldSpace() const;

    void ApplyImpulse( const glm::vec3 & impulsePoint, const glm::vec3 & impulse );
    void ApplyImpulseLinear( const glm::vec3 & impulse );
    void ApplyImpulseAngular( const glm::vec3 & impulse );

    void Update( const float dt_sec );    
};