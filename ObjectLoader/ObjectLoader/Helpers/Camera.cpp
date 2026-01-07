//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
	SetLens(0.25f * MathHelper::Pi, 1.0f, 10.0f, 2000.0f);
}

XMVECTOR Camera::GetPosition()const
{
	return XMLoadFloat3(&_position);
}

XMFLOAT3 Camera::GetPosition3F()const
{
	return _position;
}

void Camera::SetPosition(const float x, const float y, const float z)
{
	_position = XMFLOAT3(x, y, z);
	_viewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
	_position = v;
	_viewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
	return XMLoadFloat3(&_right);
}

XMFLOAT3 Camera::GetRight3F()const
{
	return _right;
}

XMVECTOR Camera::GetUp()const
{
	return XMLoadFloat3(&_up);
}

XMFLOAT3 Camera::GetUp3F()const
{
	return _up;
}

XMVECTOR Camera::GetLook()const
{
	return XMLoadFloat3(&_look);
}

XMFLOAT3 Camera::GetLook3F()const
{
	return _look;
}

float Camera::GetNearZ()const
{
	return _nearZ;
}

float Camera::GetFarZ()const
{
	return _farZ;
}

float Camera::GetAspect()const
{
	return _aspect;
}

float Camera::GetFovY()const
{
	return _fovY;
}

float Camera::GetFovX()const
{
	const float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / _nearZ);
}

float Camera::GetNearWindowWidth()const
{
	return _aspect * _nearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
	return _nearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
	return _aspect * _farWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
	return _farWindowHeight;
}

void Camera::SetLens(const float fovY, const float aspect, const float zn, const float zf)
{
	// cache properties
	_fovY = fovY;
	_aspect = aspect;
	_nearZ = zn;
	_farZ = zf;

	_nearWindowHeight = 2.0f * _nearZ * tanf(0.5f * _fovY);
	_farWindowHeight = 2.0f * _farZ * tanf(0.5f * _fovY);

	const XMMATRIX p = XMMatrixPerspectiveFovLH(_fovY, _aspect, _nearZ, _farZ);
	XMStoreFloat4x4(&_proj, p);
	auto determinant = XMMatrixDeterminant(p);
	XMStoreFloat4x4(&_invProj, XMMatrixInverse(&determinant, p));
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	const XMVECTOR l = XMVector3Normalize(XMVectorSubtract(target, pos));
	const XMVECTOR r = XMVector3Normalize(XMVector3Cross(worldUp, l));
	const XMVECTOR u = XMVector3Cross(l, r);

	XMStoreFloat3(&_position, pos);
	XMStoreFloat3(&_look, l);
	XMStoreFloat3(&_right, r);
	XMStoreFloat3(&_up, u);

	_viewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	const XMVECTOR p = XMLoadFloat3(&pos);
	const XMVECTOR t = XMLoadFloat3(&target);
	const XMVECTOR u = XMLoadFloat3(&up);

	LookAt(p, t, u);

	_viewDirty = true;
}

XMMATRIX Camera::GetView()const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_view);
}

XMMATRIX Camera::GetProj()const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_proj);
}

XMMATRIX Camera::GetPrevView() const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_prevView);
}

DirectX::XMMATRIX Camera::GetPrevProj() const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_prevProj);
}

DirectX::XMMATRIX Camera::GetInvView() const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_invView);
}

DirectX::XMMATRIX Camera::GetInvProj() const
{
	assert(!_viewDirty);
	return XMLoadFloat4x4(&_invProj);
}

DirectX::XMFLOAT4X4 Camera::GetPrevView4X4F() const
{
	assert(!_viewDirty);
	return _prevView;
}

DirectX::XMFLOAT4X4 Camera::GetPrevProj4X4F() const
{
	assert(!_viewDirty);
	return _prevProj;
}

DirectX::XMFLOAT4X4 Camera::GetPrevInvProj4X4F() const
{
	assert(!_viewDirty);
	return _prevInvProj;
}

DirectX::XMFLOAT4X4 Camera::GetInvView4X4F() const
{
	assert(!_viewDirty);
	return _invView;
}

DirectX::XMFLOAT4X4 Camera::GetInvProj4X4F() const
{
	assert(!_viewDirty);
	return _invProj;
}

DirectX::XMFLOAT4X4 Camera::GetTransposedMatrix(const XMFLOAT4X4& matrix4X4)
{
	const XMMATRIX matrix = XMLoadFloat4x4(&matrix4X4);
	const auto matrixTransposed = XMMatrixTranspose(matrix);
	XMFLOAT4X4 matrixTransposed4X4;
	XMStoreFloat4x4(&matrixTransposed4X4, matrixTransposed);
	return matrixTransposed4X4;
}

DirectX::XMFLOAT4X4 Camera::GetPrevView4X4FTransposed() const
{
	return GetTransposedMatrix(_prevView);
}

DirectX::XMFLOAT4X4 Camera::GetPrevProj4X4FTransposed() const
{
	return GetTransposedMatrix(_prevProj);
}

DirectX::XMFLOAT4X4 Camera::GetPrevInvProj4X4FTransposed() const
{
	return GetTransposedMatrix(_prevInvProj);
}

DirectX::XMFLOAT4X4 Camera::GetInvView4X4FTransposed() const
{
	return GetTransposedMatrix(_invView);
}

DirectX::XMFLOAT4X4 Camera::GetInvProj4X4FTransposed() const
{
	return GetTransposedMatrix(_invProj);
}

XMFLOAT4X4 Camera::GetView4X4F()const
{
	assert(!_viewDirty);
	return _view;
}

XMFLOAT4X4 Camera::GetProj4X4F()const
{
	assert(!_viewDirty);
	return _proj;
}

void Camera::Strafe(const float d)
{
	// mPosition += d*mRight
	const XMVECTOR s = XMVectorReplicate(d);
	const XMVECTOR r = XMLoadFloat3(&_right);
	const XMVECTOR p = XMLoadFloat3(&_position);
	XMStoreFloat3(&_position, XMVectorMultiplyAdd(s, r, p));

	_viewDirty = true;
}

void Camera::Walk(const float d)
{
	// mPosition += d*mLook
	const XMVECTOR s = XMVectorReplicate(d);
	const XMVECTOR l = XMLoadFloat3(&_look);
	const XMVECTOR p = XMLoadFloat3(&_position);
	XMStoreFloat3(&_position, XMVectorMultiplyAdd(s, l, p));

	_viewDirty = true;
}

void Camera::Pitch(const float angle)
{
	// Rotate up and look vector about the right vector.

	const XMMATRIX r = XMMatrixRotationAxis(XMLoadFloat3(&_right), angle);

	XMStoreFloat3(&_up, XMVector3TransformNormal(XMLoadFloat3(&_up), r));
	XMStoreFloat3(&_look, XMVector3TransformNormal(XMLoadFloat3(&_look), r));

	_viewDirty = true;
}

void Camera::RotateY(const float angle)
{
	// Rotate the basis vectors about the world y-axis.

	const XMMATRIX r = XMMatrixRotationY(angle);

	XMStoreFloat3(&_right, XMVector3TransformNormal(XMLoadFloat3(&_right), r));
	XMStoreFloat3(&_up, XMVector3TransformNormal(XMLoadFloat3(&_up), r));
	XMStoreFloat3(&_look, XMVector3TransformNormal(XMLoadFloat3(&_look), r));

	_viewDirty = true;
}

void Camera::UpdateViewMatrix()
{
	_prevView = _view;
	_prevProj = _proj;
	_prevInvProj = _invProj;
	
	if (_viewDirty)
	{
		XMVECTOR r = XMLoadFloat3(&_right);
		XMVECTOR l = XMLoadFloat3(&_look);
		const XMVECTOR p = XMLoadFloat3(&_position);

		// Keep camera's axes orthogonal to each other and of unit length.
		l = XMVector3Normalize(l);
		const XMVECTOR u = XMVector3Normalize(XMVector3Cross(l, r));

		// U, L already ortho-normal, so no need to normalize cross product.
		r = XMVector3Cross(u, l);

		// Fill in the view matrix entries.
		const float x = -XMVectorGetX(XMVector3Dot(p, r));
		const float y = -XMVectorGetX(XMVector3Dot(p, u));
		const float z = -XMVectorGetX(XMVector3Dot(p, l));

		XMStoreFloat3(&_right, r);
		XMStoreFloat3(&_up, u);
		XMStoreFloat3(&_look, l);

		_view(0, 0) = _right.x;
		_view(1, 0) = _right.y;
		_view(2, 0) = _right.z;
		_view(3, 0) = x;

		_view(0, 1) = _up.x;
		_view(1, 1) = _up.y;
		_view(2, 1) = _up.z;
		_view(3, 1) = y;

		_view(0, 2) = _look.x;
		_view(1, 2) = _look.y;
		_view(2, 2) = _look.z;
		_view(3, 2) = z;

		_view(0, 3) = 0.0f;
		_view(1, 3) = 0.0f;
		_view(2, 3) = 0.0f;
		_view(3, 3) = 1.0f;

		_viewDirty = false;

		XMStoreFloat4x4(&_invView, XMMatrixInverse(nullptr, GetView()));
	}
}

void Camera::UpdateFrustum()
{
	BoundingFrustum::CreateFromMatrix(_frustum, XMLoadFloat4x4(&_proj));
}

DirectX::BoundingFrustum Camera::CameraFrustum() const
{
	return _frustum;
}


