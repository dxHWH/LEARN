
/* ---------- 实验 1：归属判断 ---------- */
(function(){
  const items = [
    ["全局变量 int g_count", 1, "位于 .data/.bss 段，所有线程看到同一地址（d1 实测 &g_global 三个线程完全一致）。"],
    ["new 出来的对象", 1, "堆属于进程地址空间，一个线程 new、另一个线程 delete 是合法的。"],
    ["程序计数器 PC / RIP", 0, "每个线程执行到不同的指令，必须各有一份，切换时保存在 TCB / 内核栈中。"],
    ["函数内的局部变量", 0, "位于各自线程的栈上（d1 实测三个线程 &local 各不相同）。注意：拿到地址后其他线程仍可访问。"],
    ["打开的文件描述符 / 句柄", 1, "fd 表 / 句柄表属于进程。一个线程 close 后，其他线程再用这个 fd 就会出错。"],
    ["errno", 0, "通过 TLS 实现，每线程一份，否则一个线程的系统调用会覆盖另一个线程的错误码。"],
    ["thread_local 变量", 0, "每个线程一个副本（d1 实测地址和值都不同）。"],
    ["当前工作目录", 1, "进程级属性，一个线程 chdir 会影响所有线程的相对路径。"],
    ["函数内的 static 局部变量", 1, "虽然写在函数里，但存储在数据段，全进程只有一份。"],
    ["栈指针 SP / RSP", 0, "每个线程有自己的栈，自然有自己的栈指针。"],
    ["信号处理函数（sigaction 设置的 handler）", 1, "Linux 中信号处置方式是进程级的。"],
    ["信号屏蔽字（pthread_sigmask）", 0, "Linux 中每个线程有独立的信号屏蔽字。"],
    ["进程 ID（getpid 返回值）", 1, "同一进程的所有线程 getpid() 相同；Linux 中线程各自的 ID 是 gettid()。"],
    ["调度优先级与运行状态", 0, "调度器以线程为单位调度，每个线程都有自己的状态与优先级。"],
    ["代码段（机器指令）", 1, "只读，所有线程执行同一份代码。"],
    ["Windows GetLastError() 的值", 0, "保存在每个线程的 TEB 中。"]
  ];
  const $ = id => document.getElementById(id);
  if(!$("ow-item")) return;
  let order = items.map((_,i)=>i).sort(()=>Math.random()-0.5), k = 0, ok = 0, done = 0, answered = false;
  function show(){ const it = items[order[k % order.length]]; $("ow-item").textContent = (k % order.length + 1) + ". " + it[0]; answered = false;
    $("ow-share").disabled = $("ow-own").disabled = false; }
  function answer(v){
    if(answered) return; answered = true; done++;
    const it = items[order[k % order.length]];
    const right = (v === it[1]);
    if(right) ok++;
    $("ow-out").textContent = (right ? "✔ 正确。" : "✘ 不对。") + "答案：" + (it[1] ? "进程共享" : "线程独立") + "\n" + it[2];
    $("ow-score").textContent = "得分 " + ok + " / " + done;
    $("ow-share").disabled = $("ow-own").disabled = true;
  }
  $("ow-share").onclick = () => answer(1);
  $("ow-own").onclick = () => answer(0);
  $("ow-next").onclick = () => { k++; if(k % order.length === 0){ order.sort(()=>Math.random()-0.5); } show(); $("ow-out").textContent = "请作答。"; };
  show();
})();

/* ---------- 实验 2：状态机模拟器 ---------- */
(function(){
  const $ = id => document.getElementById(id);
  if(!$("st-cur")) return;
  const color = {"创建":"#4B5563","就绪":"#15803D","运行":"#2F5BD3","阻塞":"#B91C1C","终止":"#4B5563"};
  const events = [
    ["创建", "初始化完成，准入", "就绪", "PCB/TCB 已建好，放入就绪队列。"],
    ["就绪", "调度器选中", "运行", "上下文被装入 CPU，开始执行。"],
    ["运行", "时间片用完（时钟中断）", "就绪", "被动让出 CPU，回到就绪队列尾部，随时可以再被调度。"],
    ["运行", "被高优先级线程抢占", "就绪", "更高优先级的线程被唤醒，抢占当前 CPU。"],
    ["运行", "调用 read() 等待磁盘", "阻塞", "主动发起等待：挂到磁盘请求的等待队列上，CPU 交给别人。"],
    ["运行", "mutex.lock() 没抢到", "阻塞", "挂到这把锁的等待队列上。"],
    ["阻塞", "磁盘中断：数据到达 / 锁被释放", "就绪", "被唤醒，但不能直接运行，必须先进就绪队列排队。"],
    ["运行", "return / exit()", "终止", "释放大部分资源；Linux 中在父进程 wait 之前是僵尸（Z）。"],
    ["运行", "访问空指针（致命异常）", "终止", "SIGSEGV / 0xC0000005，整个进程被终止。"]
  ];
  let cur = "创建";
  function render(){
    $("st-cur").textContent = cur; $("st-cur").style.color = color[cur];
    const box = $("st-btns"); box.innerHTML = "";
    events.forEach(e => {
      const b = document.createElement("button"); b.className = "st-btn"; b.textContent = e[1];
      b.disabled = (e[0] !== cur);
      b.onclick = () => { log(cur + " → " + e[2] + "：" + e[3]); cur = e[2]; render(); };
      box.appendChild(b);
    });
    const r = document.createElement("button"); r.className = "st-btn"; r.textContent = "↺ 重新开始";
    r.onclick = () => { cur = "创建"; $("st-log").textContent = ""; log("新进程：fork() / CreateProcess()"); render(); };
    box.appendChild(r);
    if(cur === "终止") log("（进程已终止。注意：没有任何按钮能让阻塞直接变为运行。）");
  }
  function log(s){ const o = $("st-log"); o.textContent += (o.textContent ? "\n" : "") + s; o.scrollTop = o.scrollHeight; }
  log("新进程：fork() / CreateProcess()");
  render();
})();
