#include <stdio.h>
#include <unistd.h>
#include "icI2C.h"
#include "vc_ctrl_api.h"

int main ( void )
{
  int32_t ret = -1;
  int millisecond = 0;
  int version = 0;
  int workmode = 0;
  Command_t cmd = {0};

  if ( icI2C_Init() == 0 ) {
    printf ( "I2C init success.\n" );
  }
  usleep ( 2500000 ); // 模块启动

  version = VCGetVersion(&cmd);
  printf("Curent version:%d\n",version);

  //VCChangeOutputFormat(&cmd,WORK_MODE_TOPLIGHT);

  ret = VCChangeWorkMode(&cmd, WORK_MODE_TOPLIGHT);
  if(ret == 0) {
    printf("VCChangeWorkMode() success with WORK_MODE_TOPLIGHT.\n");
  }

  printf("Get work mode.\n");
  workmode = VCGetWorkMode(&cmd);
  printf("Current work mode:%d.\n",workmode);


  // 功能切换
  // 上电后必须先切换到录音功能。一般不运行识别和唤醒时，都使用模块的录音功能。
  ret = VCChangeFunc ( &cmd, FUNC_MODE_PASSBY );
  if(ret == 0) {
    printf("VCChangeFunc() success with FUNC_MODE_PASSBY.\n");
  }
  // 启动识别前切换到降噪功能
  ret = VCChangeFunc ( &cmd, FUNC_MODE_NOISECLEAN );
  if(ret == 0) {
    printf("VCChangeFunc() success with FUNC_MODE_NOISECLEAN.\n");
  }
#if 0
  // 启动唤醒时切换到唤醒回声消除功能
  ret = VCChangeFunc ( &cmd, FUNC_MODE_WAKEUP );
  if(ret == 0) {
    printf("VCChangeFunc() success with FUNC_MODE_WAKEUP.\n");
  }

  while ( 1 ) {
    if ( VCGetWakeupSign ( &cmd, &millisecond ) == 1 ) {
      // 处理唤醒
      printf("Process Wakeup.\n");
      break;
    }
    printf("Check Wakeup.\n");
    usleep ( 200000 );
  }

  // 打电话时切换到电话回声消除功能。通常你不需要此功能。
  // 注意你的电话系统自身集成了回声消除功能。不需要使用我们的电话回声消除。
  // 打电话时，只要把模块切换到录音功能即可。
  ret = VCChangeFunc ( &cmd, FUNC_MODE_PHONE );

  for ( ;; ) {}
#endif
}
