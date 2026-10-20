#pragma once

#include <gdiplus.h>
class CGdiPlusBitmap
{
public:
	Gdiplus::Bitmap* m_pBitmap;

public:
	CGdiPlusBitmap() { m_pBitmap = NULL; }
	CGdiPlusBitmap(LPCWSTR pFile) { m_pBitmap = NULL; Load(pFile); }
	virtual ~CGdiPlusBitmap() { Empty(); }

	void Empty() { delete m_pBitmap; m_pBitmap = NULL; }

	bool Load(LPCWSTR pFile)
	{
		Empty();
		m_pBitmap = Gdiplus::Bitmap::FromFile(pFile);
		if (NULL != m_pBitmap)
		{
			if (m_pBitmap->GetLastStatus() == Gdiplus::Ok)
				return true;
			delete m_pBitmap;
			m_pBitmap = NULL;
		}
		return false;
	}

	operator Gdiplus::Bitmap* () const { return m_pBitmap; }
};


class CGdiPlusBitmapResource : public CGdiPlusBitmap
{
protected:
	HGLOBAL m_hBuffer;

public:
	CGdiPlusBitmapResource() { m_hBuffer = NULL; }
	CGdiPlusBitmapResource(LPCTSTR pName, LPCTSTR pType = RT_RCDATA, HMODULE hInst = NULL)
	{
		m_hBuffer = NULL; Load(pName, pType, hInst);
	}
	CGdiPlusBitmapResource(UINT id, LPCTSTR pType = RT_RCDATA, HMODULE hInst = NULL)
	{
		m_hBuffer = NULL; Load(id, pType, hInst);
	}
	CGdiPlusBitmapResource(UINT id, UINT type, HMODULE hInst = NULL)
	{
		m_hBuffer = NULL; Load(id, type, hInst);
	}
	virtual ~CGdiPlusBitmapResource() { Empty(); }

	void Empty();

	bool Load(LPCWSTR pFile);
	bool Load(LPCTSTR pName, LPCTSTR pType, HMODULE hInst = NULL);
	bool Load(UINT id, LPCTSTR pType, HMODULE hInst = NULL)
	{
		return Load(MAKEINTRESOURCE(id), pType, hInst);
	}
	bool Load(UINT id, UINT type, HMODULE hInst = NULL)
	{
		return Load(MAKEINTRESOURCE(id), MAKEINTRESOURCE(type), hInst);
	}
	bool Load(char const* pData, DWORD imageSize, HMODULE hInst = NULL);
};

inline bool CGdiPlusBitmapResource::Load(LPCWSTR pFile)
{
	return CGdiPlusBitmap::Load(pFile);
}

inline
void CGdiPlusBitmapResource::Empty()
{
	CGdiPlusBitmap::Empty();
	if (m_hBuffer)
	{
		::GlobalUnlock(m_hBuffer);
		::GlobalFree(m_hBuffer);
		m_hBuffer = NULL;
	}
}

inline
bool CGdiPlusBitmapResource::Load(LPCTSTR pName, LPCTSTR pType, HMODULE hInst)
{
	Empty();

	HRSRC hResource = ::FindResource(hInst, pName, pType);
	if (!hResource)
		return false;

	DWORD imageSize = ::SizeofResource(hInst, hResource);
	if (!imageSize)
		return false;

	const void* pResourceData = ::LockResource(::LoadResource(hInst, hResource));
	if (!pResourceData)
		return false;

	m_hBuffer = ::GlobalAlloc(GMEM_MOVEABLE, imageSize);
	if (m_hBuffer)
	{
		void* pBuffer = ::GlobalLock(m_hBuffer);
		if (pBuffer)
		{
			CopyMemory(pBuffer, pResourceData, imageSize);

			IStream* pStream = NULL;
			if (::CreateStreamOnHGlobal(pBuffer, FALSE, &pStream) == S_OK)
			{
				m_pBitmap = Gdiplus::Bitmap::FromStream(pStream);
				pStream->Release();
				if (m_pBitmap)
				{
					if (m_pBitmap->GetLastStatus() == Gdiplus::Ok)
						return true;

					delete m_pBitmap;
					m_pBitmap = NULL;
				}
			}
			::GlobalUnlock(pBuffer);
		}
		::GlobalFree(m_hBuffer);
		m_hBuffer = NULL;
	}
	return false;
}

inline
bool CGdiPlusBitmapResource::Load(char const* pData, DWORD imageSize, HMODULE hInst)
{
	Empty();
	m_hBuffer = ::GlobalAlloc(GMEM_MOVEABLE, imageSize);
	if (m_hBuffer)
	{
		void* pBuffer = ::GlobalLock(m_hBuffer);
		if (pBuffer)
		{
			CopyMemory(pBuffer, pData, imageSize);

			IStream* pStream = NULL;
			if (::CreateStreamOnHGlobal(pBuffer, FALSE, &pStream) == S_OK)
			{
				m_pBitmap = Gdiplus::Bitmap::FromStream(pStream);
				pStream->Release();
				if (m_pBitmap)
				{
					if (m_pBitmap->GetLastStatus() == Gdiplus::Ok)
						return true;

					delete m_pBitmap;
					m_pBitmap = NULL;
				}
			}
			::GlobalUnlock(pBuffer);
		}
		::GlobalFree(m_hBuffer);
		m_hBuffer = NULL;
	}
	return false;
}
