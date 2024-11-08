#include "tank.h"

#include "./collision/collision_scene.h"
#include "./collision/box.h"
#include "./rampage.h"
#include "./util/entity_id.h"
#include "./math/quaternion.h"
#include "./assets.h"
#include "./math/mathf.h"

struct Vector2 tank_rotate_speed;

#define TANK_SPEED  SCALE_FIXED_POINT(0.5f)
#define TANK_ACCEL  SCALE_FIXED_POINT(0.5f)

struct dynamic_object_type tank_collider = {
    .minkowsi_sum = box_minkowski_sum,
    .bounding_box = box_bounding_box,
    .data = {
        .box = {
            .half_size = {
                SCALE_FIXED_POINT(1.06136f * 0.5f),
                SCALE_FIXED_POINT(0.63024f * 0.5f),
                SCALE_FIXED_POINT(1.27636f * 0.5f),
            }
        },
    },
    .bounce = 0.0f,
    .friction = 0.0f,
};

void rampage_tank_init(struct RampageTank* tank, struct Vector3* start_position) {
    int entity_id = entity_id_next();
    dynamic_object_init(
        entity_id,
        &tank->dynamic_object,
        &tank_collider,
        COLLISION_LAYER_TANGIBLE,
        start_position,
        &gRight2
    );

    tank->dynamic_object.center.y = tank_collider.data.box.half_size.y;

    collision_scene_add(&tank->dynamic_object);

    vector2ComplexFromAngle(1.0f / 30.0f, &tank_rotate_speed);
}

void rampage_tank_destroy(struct RampageTank* tank) {

}

void rampage_tank_update(struct RampageTank* tank, float delta_time) {
    struct Vector2 offset = (struct Vector2){
        tank->current_target.x - tank->dynamic_object.position.x,
        tank->current_target.z - tank->dynamic_object.position.z,
    };

    struct Vector2 dir;
    vector2Normalize(&offset, &dir);

    struct Vector2 current_dir = (struct Vector2){
        tank->dynamic_object.rotation.y,
        tank->dynamic_object.rotation.x,
    };

    if (vector2RotateTowards(
        &current_dir,
        &dir,
        &tank_rotate_speed,
        &current_dir
    )) {
        // float distance = vector2Dot(&offset, &dir);
        // float speed = dir.x * tank->dynamic_object.velocity.x +
        //     dir.y * tank->dynamic_object.velocity.z;

        tank->dynamic_object.velocity.x = mathfMoveTowards(
            tank->dynamic_object.velocity.x,
            dir.x * TANK_SPEED,
            TANK_ACCEL * delta_time
        );
        tank->dynamic_object.velocity.z = mathfMoveTowards(
            tank->dynamic_object.velocity.z,
            dir.y * TANK_SPEED,
            TANK_ACCEL * delta_time
        );
    } else {
        tank->dynamic_object.velocity.x = mathfMoveTowards(
            tank->dynamic_object.velocity.x,
            0.0f,
            TANK_ACCEL * delta_time
        );
        tank->dynamic_object.velocity.z = mathfMoveTowards(
            tank->dynamic_object.velocity.z,
            0.0f,
            TANK_ACCEL * delta_time
        );
    }

    tank->dynamic_object.rotation.x = current_dir.y;
    tank->dynamic_object.rotation.y = current_dir.x;
}

void rampage_tank_render(struct RampageTank* tank) {
    struct Quaternion quat;
    quatAxisComplex(&gUp, &tank->dynamic_object.rotation, &quat);
    T3DVec3 scale = {{1.0f, 1.0f, 1.0f}};

    t3d_mat4fp_from_srt(UncachedAddr(&tank->mtx), scale.v, (float*)&quat, (float*)&tank->dynamic_object.position);
    t3d_matrix_push(&tank->mtx);
    rspq_block_run(rampage_assets_get()->tankSplit.mesh);
    t3d_matrix_pop(1);
}