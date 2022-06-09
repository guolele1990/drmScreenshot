#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "assert.h"

#if ENABLE_FB_BUFFER_WRITE
struct buffer_object {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
	uint32_t size;
	uint8_t *vaddr;
	uint32_t fb_id;
};

struct buffer_object buf;

static int modeset_create_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};

	create.width = bo->width;
	create.height = bo->height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

	bo->pitch = create.pitch;
	bo->size = create.size;
	bo->handle = create.handle;
	drmModeAddFB(fd, bo->width, bo->height, 24, 32, bo->pitch,
			   bo->handle, &bo->fb_id);

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

	bo->vaddr = mmap(0, create.size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, map.offset);

	memset(bo->vaddr, 0x55, bo->size);

	return 0;
}

static void modeset_destroy_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_destroy_dumb destroy = {};

	drmModeRmFB(fd, bo->fb_id);

	munmap(bo->vaddr, bo->size);

	destroy.handle = bo->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}
#endif

void *mmap_dumb_bo(int fd, int handle, size_t size)
{
	struct drm_mode_map_dumb mmap_arg;
	void *ptr;
	int ret;
	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
	//perror(" tdrmIoctl error\n");
	assert(ret == 0);
	assert(mmap_arg.offset != 0);
	ptr = mmap(NULL, size, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, mmap_arg.offset);
	return ptr;
}

#pragma pack(1)
 
typedef struct{
    short type;
    int size;
    short reserved1;
    short reserved2;
    int offset;
} BMPHeader;
 
typedef struct{
    int size;
    int width;
    int height;
    short planes;
    short bitsPerPixel;
    unsigned compression;
    unsigned imageSize;
    int xPelsPerMeter;
    int yPelsPerMeter;
    int clrUsed;
    int clrImportant;
} BMPInfoHeader;
 
#pragma pack()
 
//src is U8C3_PLANNER,BBBB...GGGG...RRR,BMP need to be BGR...BGR...BGR
int saveBMPFile(unsigned char *pRGBData, int width, int height, int bpp,const char* name)
{
	BMPHeader hdr;
	BMPInfoHeader infoHdr;
    int ret = 0;
 	unsigned char* src = pRGBData;
	FILE* fp = NULL;
    if(NULL == src) 
    {
		return (-1);
    }
 
    fp = fopen(name,"wb");
	if(NULL == fp)
    {
		printf("saveBMPFile: Err: Open!\n");
		return (-1);
	}
 
	infoHdr.size	= sizeof(BMPInfoHeader);
	infoHdr.width	= width;
	infoHdr.height	= 0 - height;
	infoHdr.planes	= 1;
	infoHdr.bitsPerPixel	= bpp;
	infoHdr.compression		= 0;
	infoHdr.imageSize		= width*height;
	infoHdr.xPelsPerMeter	= 0;
	infoHdr.yPelsPerMeter	= 0;
	infoHdr.clrUsed			= 0;
	infoHdr.clrImportant	= 0;
 
	hdr.type	= 0x4D42;
	hdr.size	= 54 + infoHdr.imageSize*3;
	hdr.reserved1	= 0;
	hdr.reserved2	= 0;
	hdr.offset	= 54;
 
 
    //BGR U8C3_PLANNER to BGR...BGR...BGR
    fwrite(&hdr, sizeof(hdr), 1, fp);
  	fwrite(&infoHdr, sizeof(infoHdr), 1, fp);
	#if 1//save rgb32
	fwrite(src, width*height*bpp/8,1, fp);//abgr
	#else
	if(srcImage.type == HI_SVP_IMG_TYPE_U8C3_PLANAR)
	{
	 	int i;
	    int temp;
	    for(i = 0;i < width*height;i++)
	    {
	        fwrite(src+i, sizeof(unsigned char), 1, fp);//B
			fwrite(src+width*height+i, sizeof(unsigned char), 1, fp);//G
			fwrite(src+width*height*2+i, sizeof(unsigned char), 1, fp);//R
	    }
	}
	#endif
	fflush(fp);
	if(ferror(fp)){
		printf("saveBMPFile: Err: Unknown!***\n");
	}
 
	fclose(fp);
    fp = NULL;
 
	return 0;
}


int main(int argc, char **argv)
{
	int fd;
	drmModeConnector *conn;
	drmModeRes *res;
	drmModePlaneRes *plane_res;
	uint32_t conn_id;
	uint32_t crtc_id;
	uint32_t plane_id;

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

	res = drmModeGetResources(fd);
	crtc_id = res->crtcs[0];
	conn_id = res->connectors[0];

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	plane_res = drmModeGetPlaneResources(fd);
	
	for(unsigned int i =0;i < plane_res->count_planes;i++)
	{
		printf("planes %d = %d\r\n",i,plane_res->planes[i]);
	}
	plane_id = plane_res->planes[0];

	conn = drmModeGetConnector(fd, conn_id);
	#if ENABLE_FB_BUFFER_WRITE
	buf.width = conn->modes[0].hdisplay;
	buf.height = conn->modes[0].vdisplay;

	modeset_create_fb(fd, &buf);
	printf("modeset_create_fb\r\n");
	//getchar();
	drmModeSetPlane(fd, plane_id, crtc_id, buf.fb_id, 0,
			0, 0, 1920, 1080,
			100 << 16, 150 << 16, 320 << 16, 320 << 16);
	drmModeSetCrtc(fd, crtc_id, buf.fb_id,
			0, 0, &conn_id, 1, &conn->modes[0]);
	printf("drmModeSetCrtc\r\n");

	//getchar();
	memset(buf.vaddr, 0x99, buf.size);

	/* crop the rect from framebuffer(100, 150) to crtc(50, 50) */
	drmModeSetPlane(fd, plane_id, crtc_id, buf.fb_id, 0,
			50, 50, 1920, 1080,
			100 << 16, 150 << 16, 320 << 16, 320 << 16);
	printf("drmModeSetPlane\r\n");
	#endif
	//getchar();
	while(1)
	{
		//FILE *yuvFile = fopen("./out.rgb","wb+");
		drmModePlane *plane;
		unsigned int fb_id = 0; 
		plane = drmModeGetPlane(fd, plane_id);
		if (!plane) {
		    printf("Failed to get plane %d\n", plane_id);
		    return -1;
		}
		fb_id = plane->fb_id;
		printf("fb_id %d \r\n",fb_id);
		drmModeFBPtr fb =  drmModeGetFB(fd, fb_id);
		int dma_buf_fd = -1;
		if (!fb) {
			printf("Cannot open fb %#x\r\n", fb_id);
			return 0;
		}

		printf("fb_id=%#x width=%u height=%u pitch=%u bpp=%u depth=%u handle=%#x\r\n",
			fb_id, fb->width, fb->height, fb->pitch, fb->bpp, fb->depth, fb->handle);

		if (!fb->handle) {
			printf("Not permitted to get fb handles. Run either with sudo, or setcap cap_sys_admin+ep %s\r\n", argv[0]);
			return 0;
		}

		long bo_size = fb->width*fb->height*fb->bpp/8;

		int ret = drmPrimeHandleToFD(fd, fb->handle, O_RDONLY, &dma_buf_fd);
		printf("drmPrimeHandleToFD = %d, fd = %d\r\n", ret, dma_buf_fd);
		volatile uint32_t *bo_ptr = mmap_dumb_bo(fd, fb->handle, bo_size);
		if (bo_ptr == MAP_FAILED) {
			fprintf(stderr, " to map imported buffer object\n");
			return -1;
		}
		printf("bo_ptr %p\r\n",bo_ptr);
		//int writeLen = fwrite(bo_ptr,1,bo_size,yuvFile);
		//printf("writeLen %d\r\n",writeLen);
		saveBMPFile((unsigned char*)bo_ptr,fb->width,fb->height,fb->bpp,"./screen.bmp");
		//fflush(yuvFile);
		//fclose(yuvFile);
		assert(!munmap((uint32_t *)bo_ptr, bo_size));
		drmModeFreeFB(fb);
		drmModeFreePlane(plane);
		getchar();
		system("rm ./screen.bmp");
	}
	printf("screen shot\r\n");
	//getchar();
	#if ENABLE_FB_BUFFER_WRITE
	modeset_destroy_fb(fd, &buf);
	#endif	

	drmModeFreeConnector(conn);
	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);

	close(fd);

	return 0;
}

