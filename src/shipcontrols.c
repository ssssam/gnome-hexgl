#include "shipcontrols.h"

#define EPSILON 0.00000001

struct _ShipControls {
  GthreeObject *mesh;
  GthreeObject *dummy;
  AnalysisMap *height_map;

  gboolean active;
  gboolean destroyed;
  gboolean falling;

  float airResist;
  float airDrift;
  float thrust;
  float airBrake;
  float maxSpeed;
  float boosterSpeed;
  float boosterDecay;
  float angularSpeed;
  float airAngularSpeed;
  float repulsionRatio;
  float repulsionCap;
  float repulsionLerp;
  float collisionSpeedDecrease;
  float collisionSpeedDecreaseCoef;
  float maxShield;
  float shieldDelay;
  float shieldTiming;
  float shieldDamage;
  float driftLerp;
  float angularLerp;
  float rollAngle;
  float rollLerp;
  float heightLerp;
  float heightOffset; // height above track
  float gradientLerp;
  float gradientScale;
  float tiltLerp;
  float tiltScale;

  /* motion state */
  float drift;
  float angular;
  float speed;
  float speedRatio;
  float roll;
  graphene_vec3_t repulsionForce;
  float gradient;
  float gradientTarget;
  float tilt;
  float tiltTarget;

  gboolean key_forward;
  gboolean key_backward;
  gboolean key_left;
  gboolean key_right;
  gboolean key_ltrigger;
  gboolean key_rtrigger;
  gboolean key_use;
};

ShipControls *
ship_controls_new (void)
{
  ShipControls *controls = g_new0 (ShipControls, 1);

  controls->active = TRUE;

  controls->airResist = 0.02;
  controls->airDrift = 0.1;
  controls->thrust = 0.02;
  controls->airBrake = 0.02;
  controls->maxSpeed = 7.0;
  controls->boosterSpeed = controls->maxSpeed * 0.2;
  controls->boosterDecay = 0.01;
  controls->angularSpeed = 0.005;
  controls->airAngularSpeed = 0.0065;
  controls->repulsionRatio = 0.5;
  controls->repulsionCap = 2.5;
  controls->repulsionLerp = 0.1;
  controls->collisionSpeedDecrease = 0.8;
  controls->collisionSpeedDecreaseCoef = 0.8;
  controls->maxShield = 1.0;
  controls->shieldDelay = 60;
  controls->shieldTiming = 0;
  controls->shieldDamage = 0.25;
  controls->driftLerp = 0.35;
  controls->angularLerp = 0.35;
  controls->rollAngle = 0.6;
  controls->rollLerp = 0.08;
  controls->heightLerp = 0.4;
  controls->heightOffset = 4;
  controls->gradientLerp = 0.05;
  controls->gradientScale = 4.0;
  controls->tiltLerp = 0.05;
  controls->tiltScale = 4.0;

  controls->drift = 0.0;
  controls->angular = 0.0;
  controls->speed = 0.0;
  controls->speedRatio = 0.0;
  controls->roll = 0.0;
  graphene_vec3_init (&controls->repulsionForce, 0, 0, 0);
  controls->gradient = 0.0;
  controls->gradientTarget = 0.0;
  controls->tilt = 0.0;
  controls->tiltTarget = 0.0;

  controls->dummy = gthree_object_new ();
  return controls;
}

float
ship_controls_get_speed_ratio (ShipControls *controls)
{
  return controls->speedRatio;
}

void
ship_controls_control (ShipControls *controls,
                       GthreeObject *mesh)
{
  g_clear_object (&controls->mesh);
  controls->mesh = g_object_ref (mesh);

  gthree_object_set_matrix_auto_update (controls->mesh, FALSE);
  gthree_object_set_position_vec3 (controls->dummy,
                                   gthree_object_get_position (mesh));
}

void
ship_controls_set_height_map (ShipControls *controls,
                              AnalysisMap *map)
{
  controls->height_map = analysis_map_ref (map);
}

void
ship_controls_free (ShipControls *controls)
{
  g_clear_object (&controls->mesh);
  g_object_unref (controls->dummy);
  g_free (controls);
}

// TODO: Move to graphene
static graphene_quaternion_t *
multiply_quaternions (const graphene_quaternion_t *a,
                      const graphene_quaternion_t *b,
                      graphene_quaternion_t *res)
{
  // from http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/code/index.htm
  graphene_vec4_t va, vb;
  graphene_quaternion_to_vec4 (a, &va);
  graphene_quaternion_to_vec4 (b, &vb);

  float qax = graphene_vec4_get_x (&va), qay = graphene_vec4_get_y (&va), qaz = graphene_vec4_get_z (&va), qaw = graphene_vec4_get_w (&va);
  float qbx = graphene_vec4_get_x (&vb), qby = graphene_vec4_get_y (&vb), qbz = graphene_vec4_get_z (&vb), qbw = graphene_vec4_get_w (&vb);

  float x = qax * qbw + qaw * qbx + qay * qbz - qaz * qby;
  float y = qay * qbw + qaw * qby + qaz * qbx - qax * qbz;
  float z = qaz * qbw + qaw * qbz + qax * qby - qay * qbx;
  float w = qaw * qbw - qax * qbx - qay * qby - qaz * qbz;

  return graphene_quaternion_init (res, x, y, z, w);
}

#define RAD_TO_DEG(x)          ((x) * (180.f / GRAPHENE_PI))


static void
ship_controls_height_check (ShipControls *controls,
                            graphene_point3d_t *movement,
                            float dt)
{
  graphene_point3d_t pos;

  if (controls->height_map == NULL)
    return;

  graphene_point3d_init_from_vec3 (&pos,
                                   gthree_object_get_position (controls->dummy));

  float height = analysis_map_lookup_depthmapped (controls->height_map, pos.x, pos.z);

  float delta = (height + controls->heightOffset - pos.y);

  if (delta > 0)
    movement->y += delta;
  else
    movement->y += delta * controls->heightLerp;

  // gradient
  graphene_vec3_t gradientVector;
  graphene_vec3_init (&gradientVector, 0, 0, 5);

  graphene_matrix_transform_vec3 (gthree_object_get_matrix (controls->dummy),
                                  &gradientVector, &gradientVector);
  graphene_vec3_add (&gradientVector,
                     gthree_object_get_position (controls->dummy),
                     &gradientVector);

  float nheight = analysis_map_lookup_depthmapped (controls->height_map,
                                                   graphene_vec3_get_x (&gradientVector),
                                                   graphene_vec3_get_z (&gradientVector));
  if (fabs (nheight - height) < 100)
      controls->gradientTarget = -atan2f (nheight - height, 5.0); //TODO: original had this???: * controls->gradientScale;

  // tilt
  graphene_vec3_t tiltVector;
  graphene_vec3_init (&tiltVector, 5, 0, 0);

  graphene_matrix_transform_vec3 (gthree_object_get_matrix (controls->dummy),
                                  &tiltVector, &tiltVector);
  graphene_vec3_add (&tiltVector,
                     gthree_object_get_position (controls->dummy),
                     &tiltVector);

  nheight = analysis_map_lookup_depthmapped (controls->height_map,
                                             graphene_vec3_get_x (&tiltVector),
                                             graphene_vec3_get_z (&tiltVector));

  if (fabs (nheight - height) > 100) // If right project out of bounds, try left projection
    {
      graphene_vec3_init (&tiltVector, -5, 0, 0);
      graphene_matrix_transform_vec3 (gthree_object_get_matrix (controls->dummy),
                                      &tiltVector, &tiltVector);
      graphene_vec3_add (&tiltVector,
                         gthree_object_get_position (controls->dummy),
                         &tiltVector);

      nheight = analysis_map_lookup_depthmapped (controls->height_map,
                                                 graphene_vec3_get_x (&tiltVector),
                                                 graphene_vec3_get_z (&tiltVector));
      // flip tilt???
    }

  if (fabs (nheight - height) < 100)
    controls->tiltTarget = atan2f (nheight - height, 5.0); // *controls->tiltScale;
}


void
ship_controls_update (ShipControls *controls,
                      float dt)
{
  graphene_point3d_t rotation = { 0, 0, 0 };
  graphene_point3d_t movement = { 0, 0, 0 };
  graphene_quaternion_t quaternion;
  graphene_euler_t euler;

  if (controls->falling)
    {
      graphene_vec3_t pos, fall_vec;

      graphene_vec3_init (&fall_vec, 0, -20, 0);
      graphene_vec3_add (gthree_object_get_position (controls->mesh), &fall_vec, &pos);

      gthree_object_set_position_vec3 (controls->mesh, &pos);

      return;
    }

  rotation.y = 0;
  controls->drift += (0.0 - controls->drift) * controls->driftLerp;
  controls->angular += (0.0 - controls->angular) * controls->angularLerp * 0.5;

  float rollAmount = 0.0;
  float angularAmount = 0.0;

  if (controls->active)
    {
      if (controls->key_left)
        {
          angularAmount += controls->angularSpeed * dt;
          rollAmount -= controls->rollAngle;
        }

      if (controls->key_right)
        {
          angularAmount -= controls->angularSpeed * dt;
          rollAmount += controls->rollAngle;
        }

      if (controls->key_forward)
        controls->speed += controls->thrust * dt;
      else
        controls->speed -= controls->airResist * dt;

      if (controls->key_ltrigger)
        {
          if (controls->key_left)
            angularAmount += controls->airAngularSpeed * dt;
          else
            angularAmount += controls->airAngularSpeed * 0.5 * dt;

          controls->speed -= controls->airBrake * dt;
          controls->drift += (controls->airDrift - controls->drift) * controls->driftLerp;
          movement.x += controls->speed * controls->drift * dt;

          if(controls->drift > 0.0)
            movement.z -= controls->speed * controls->drift * dt;

          rollAmount -= controls->rollAngle * 0.7;
        }

      if (controls->key_rtrigger)
        {
          if(controls->key_right)
            angularAmount -= controls->airAngularSpeed * dt;
          else
            angularAmount -= controls->airAngularSpeed * 0.5 * dt;
          controls->speed -= controls->airBrake * dt;
          controls->drift += (-controls->airDrift - controls->drift) * controls->driftLerp;
          movement.x += controls->speed * controls->drift * dt;
          if(controls->drift < 0.0)
            movement.z += controls->speed * controls->drift * dt;
          rollAmount += controls->rollAngle * 0.7;
        }
    }

  controls->angular += (angularAmount - controls->angular) * controls->angularLerp;
  rotation.y = controls->angular;

  controls->speed = fmax (0.0, fmin (controls->speed, controls->maxSpeed));
  controls->speedRatio = controls->speed / controls->maxSpeed;
  movement.z += controls->speed * dt;

  if (graphene_vec3_near (&controls->repulsionForce, graphene_vec3_zero (), EPSILON))
    graphene_vec3_init (&controls->repulsionForce, 0, 0, 0);
  else
    {
      graphene_vec3_t m, v;

      if (graphene_vec3_get_z (&controls->repulsionForce) != 0.0)
        graphene_vec3_init (&controls->repulsionForce,
                            graphene_vec3_get_x (&controls->repulsionForce),
                            graphene_vec3_get_y (&controls->repulsionForce),
                            0);

      graphene_point3d_to_vec3 (&movement, &v);
      graphene_vec3_add (&v, &controls->repulsionForce, &m);
      graphene_point3d_init_from_vec3 (&movement, &m);

      graphene_vec3_scale (&controls->repulsionForce,
                           dt > 1.5 ? controls->repulsionLerp*2 : controls->repulsionLerp,
                           &v),

        graphene_vec3_subtract (&controls->repulsionForce, &v, &controls->repulsionForce);
    }

  //controls->collisionPreviousPosition.copy(controls->dummy.position);

  //controls->boosterCheck(dt);

  gthree_object_translate_x (controls->dummy, movement.x);
  gthree_object_translate_z (controls->dummy, movement.z);

  ship_controls_height_check (controls, &movement, dt);
  gthree_object_translate_y (controls->dummy, movement.y);

  //controls->currentVelocity.copy(controls->dummy.position).subSelf(controls->collisionPreviousPosition);

  //controls->collisionCheck(dt);

  graphene_quaternion_init_from_euler (&quaternion,
                                       graphene_euler_init_from_radians (&euler, rotation.x, -rotation.y, rotation.z, GRAPHENE_EULER_ORDER_DEFAULT));
  //graphene_quaternion_normalize (&quaternion, &quaternion);
  multiply_quaternions (&quaternion,
                        gthree_object_get_quaternion (controls->dummy),
                        &quaternion);
  gthree_object_set_quaternion (controls->dummy, &quaternion);
  gthree_object_update_matrix (controls->dummy);

  /*
  if (controls->shield <= 0.0)
    {
      controls->shield = 0.0;
      controls->destroy();
    }
  */

  if (controls->mesh)
    {
      graphene_matrix_t matrix;

      graphene_matrix_init_identity (&matrix);

      // Gradient (Mesh only, no dummy physics impact)
      float gradientDelta = (controls->gradientTarget - (controls->gradient)) * controls->gradientLerp;

      if (fabs (gradientDelta) > EPSILON)
        controls->gradient += gradientDelta;

      if (fabs(controls->gradient) > EPSILON)
        graphene_matrix_rotate_x (&matrix, RAD_TO_DEG (controls->gradient));

      // Tilting (Idem)
      float tiltDelta = (controls->tiltTarget - controls->tilt) * controls->tiltLerp;

      if (fabs (tiltDelta) > EPSILON)
        controls->tilt += tiltDelta;

      if (fabs (controls->tilt) > EPSILON)
        graphene_matrix_rotate_z (&matrix, RAD_TO_DEG (controls->tilt));

      // Rolling (Idem)
      float rollDelta = (rollAmount - controls->roll) * controls->rollLerp;
      if (fabs (rollDelta) > EPSILON)
        controls->roll += rollDelta;
      if (fabs (controls->roll) > EPSILON)
        {
          graphene_matrix_rotate_z (&matrix, RAD_TO_DEG (controls->roll));
        }

      graphene_matrix_multiply (&matrix,
                                gthree_object_get_matrix (controls->dummy),
                                &matrix);

      gthree_object_set_matrix (controls->mesh, &matrix);
      gthree_object_update_matrix_world (controls->mesh, TRUE);
    }

  //Update listener position
  //bkcore.Audio.setListenerPos(controls->movement);
  //bkcore.Audio.setListenerVelocity(controls->currentVelocity);

}

static gboolean
handle_key (ShipControls *controls,
            GdkEventKey *event,
            gboolean down)
{
  switch (event->keyval)
    {
    case GDK_KEY_Left:
      controls->key_left = down;
      return TRUE;
    case GDK_KEY_Right:
      controls->key_right = down;
      return TRUE;
    case GDK_KEY_Up:
      controls->key_forward = down;
      return TRUE;
    case GDK_KEY_Down:
      controls->key_backward = down;
      return TRUE;
    case GDK_KEY_A:
    case GDK_KEY_a:
      controls->key_ltrigger = down;
      return TRUE;
    case GDK_KEY_S:
    case GDK_KEY_s:
      controls->key_rtrigger = down;
      return TRUE;
    }

  return FALSE;
}

gboolean
ship_controls_key_press (ShipControls *controls,
                         GdkEventKey *event)
{
  return handle_key (controls, event, TRUE);
}

gboolean
ship_controls_key_release (ShipControls *controls,
                           GdkEventKey *event)
{
  return handle_key (controls, event, FALSE);
}
