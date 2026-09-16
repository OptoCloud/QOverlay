#include "vr/transform.h"

#include "vr/math.h"
#include "vr/conversion.h"

QOverlay::VR::Transform::Transform()
	: mat()
	, rot()
	, pos(0.0f)
{}

QOverlay::VR::Transform::Transform(const glm::mat4& matrix)
	: mat(matrix)
	, rot(glm::quat_cast(matrix))
	, pos(matrix[3])
{}

QOverlay::VR::Transform::Transform(const vr::HmdMatrix34_t& matrix)
	: Transform(Conversion::ToGlmMat(matrix))
{}

QOverlay::VR::Transform::Transform(const glm::vec3& position, const glm::quat& rotation)
	: mat(Math::CreateGlmMat(position, rotation))
	, rot(rotation)
	, pos(position)
{}

QOverlay::VR::Transform::Transform(const glm::vec3& position, const glm::vec3& rotation)
	: Transform(position, glm::quat(rotation))
{}

void QOverlay::VR::Transform::SetPosition(const glm::vec3& position) {
	mat = Math::CreateGlmMat(position, rot);
	pos = position;
}

void QOverlay::VR::Transform::SetRotation(const glm::quat& rotation) {
	mat = Math::CreateGlmMat(pos, rotation);
	rot = rotation;
}

glm::vec3 QOverlay::VR::Transform::RotationEuler() const {
	return glm::eulerAngles(rot);
}

void QOverlay::VR::Transform::SetRotationEuler(const glm::vec3& rotation) {
	SetRotation(glm::quat(rotation));
}

vr::HmdMatrix34_t QOverlay::VR::Transform::ToHmdMatrix() const {
	return Conversion::ToHmdMat(mat);
}