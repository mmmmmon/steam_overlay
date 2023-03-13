<div align="center">

# SteamOverlay

[简体中文](README-zh.md) or [English](README.md)  

</div>

- This Library achieves the purpose of overlaying a layer by hooking the SwapChain->Present function through GameOverlayRenderer64.dll hooking.

- But it is vulnerable.There are many means to detect it.（Stack backtracing 、code check、shoot screen ......）

- If you want to renew pattern code,watching the follow image,you can quickly find this code segment.

<img  src="img/1.png">
