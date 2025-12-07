//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Simple first person style camera class that lets the viewer explore the 3D scene.
//   -It keeps track of the camera coordinate system relative to the world space
//    so that the view matrix can be constructed.  
//   -It keeps track of the viewing frustum of the camera so that the projection
//    matrix can be obtained.
//***************************************************************************************

#ifndef CAMERA_H
#define CAMERA_H

#include "./../../../Common/d3dUtil.h"
#include <DirectXCollision.h>

class Camera
{
public:
	Camera();

	// Get/Set world camera position.
	DirectX::XMVECTOR GetPosition()const;
	DirectX::XMFLOAT3 GetPosition3F()const;
	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& v);

	// Get camera basis vectors.
	DirectX::XMVECTOR GetRight()const;
	DirectX::XMFLOAT3 GetRight3F()const;
	DirectX::XMVECTOR GetUp()const;
	DirectX::XMFLOAT3 GetUp3F()const;
	DirectX::XMVECTOR GetLook()const;
	DirectX::XMFLOAT3 GetLook3F()const;

	// Get frustum properties.
	float GetNearZ()const;
	float GetFarZ()const;
	float GetAspect()const;
	float GetFovY()const;
	float GetFovX()const;

	// Get near and far plane dimensions in view space coordinates.
	float GetNearWindowWidth()const;
	float GetNearWindowHeight()const;
	float GetFarWindowWidth()const;
	float GetFarWindowHeight()const;

	// Set frustum.
	void SetLens(float fovY, float aspect, float zn, float zf);

	// Define camera space via LookAt parameters.
	void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
	void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

	// Get View/Proj matrices.
	DirectX::XMMATRIX GetView()const;
	DirectX::XMMATRIX GetProj()const;
	DirectX::XMMATRIX GetInvView()const;
	DirectX::XMMATRIX GetInvProj()const;

	DirectX::XMFLOAT4X4 GetView4X4F()const;
	DirectX::XMFLOAT4X4 GetProj4X4F()const;
	DirectX::XMFLOAT4X4 GetPrevView4X4F()const;
	DirectX::XMFLOAT4X4 GetPrevProj4X4F()const;
	DirectX::XMFLOAT4X4 GetPrevInvProj4X4F()const;
	DirectX::XMFLOAT4X4 GetInvView4X4F()const;
	DirectX::XMFLOAT4X4 GetInvProj4X4F()const;

	// Strafe/Walk the camera a distance d.
	void Strafe(float d);
	void Walk(float d);

	// Rotate the camera.
	void Pitch(float angle);
	void RotateY(float angle);

	// After modifying camera position/orientation, call to rebuild the view matrix.
	void UpdateViewMatrix();
	void UpdateFrustum();

	DirectX::BoundingFrustum CameraFrustum() const;
private:

	// Camera coordinate system with coordinates relative to world space.
	DirectX::XMFLOAT3 _position = { 0.0f, 5.0f, -10.0f };
	DirectX::XMFLOAT3 _right = { 1.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 _up = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 _look = { 0.0f, 0.0f, 1.0f };

	// Cache frustum properties.
	float _nearZ = 0.0f;
	float _farZ = 0.0f;
	float _aspect = 0.0f;
	float _fovY = 0.0f;
	float _nearWindowHeight = 0.0f;
	float _farWindowHeight = 0.0f;

	bool _viewDirty = true;

	// Cache View/Proj matrices.
	DirectX::XMFLOAT4X4 _view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _invView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _invProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _prevInvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _prevView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _prevProj = MathHelper::Identity4x4();

	DirectX::BoundingFrustum _frustum;
};

#endif // CAMERA_H