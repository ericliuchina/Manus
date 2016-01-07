/*
   Copyright 2015 Manus VR

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */
 
#pragma once
class ManusMath
{
public:
	/*! \brief Convert a Quaternion to Euler angles.
	*
	*  Returns the Quaternion as Yaw, Pitch and Roll angles
	*  relative to the Earth's gravity.
	*
	*  \param euler Output variable to receive the Euler angles.
	*  \param quaternion The quaternion to convert.
	*/
	static int GetEuler(GLOVE_VECTOR* euler, const GLOVE_QUATERNION* quaternion);

	/*! \brief Remove gravity from acceleration vector.
	*
	*  Returns the Acceleration as a vector independent from
	*  the Earth's gravity.
	*
	*  \param linear Output vector to receive the linear acceleration.
	*  \param acceleation The acceleration vector to convert.
	*/
	static int GetLinearAcceleration(GLOVE_VECTOR* linear, const GLOVE_VECTOR* acceleration, const GLOVE_VECTOR* gravity);

	/*! \brief Return gravity vector from the Quaternion.
	*
	*  Returns an estimation of the Earth's gravity vector.
	*
	*  \param gravity Output vector to receive the gravity vector.
	*  \param quaternion The quaternion to base the gravity vector on.
	*/
	static int GetGravity(GLOVE_VECTOR* gravity, const GLOVE_QUATERNION* quaternion);


	static GLOVE_QUATERNION QuaternionMultiply(GLOVE_QUATERNION q1, GLOVE_QUATERNION q2);

private:
	ManusMath();
};

