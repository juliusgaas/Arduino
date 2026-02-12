
    

   let lastCmd = null; // debounce Arduino command
    let lastState = null; // 'OPEN' or 'CLOSED'
    
    /* ================= ARDUINO ================= */
    function sendCmd(cmd) {
      fetch("http://192.168.1.192:5000/control?cmd=" + cmd)
      .then(res => res.text())
      .then(data => console.log(data))
      .catch(err => console.error(err));
    }

    function refreshFaces() {
      // hidden full capture (optional)
      document.getElementById("capture").src =
        "/static/capture.jpg?t=" + Date.now();

      const container = document.getElementById("faces");
      container.innerHTML = "";

      for (let i = 1; i <=10; i++) {
        let img = new Image();
        img.src = `/static/faces/face_${i}.jpg?t=` + Date.now();
        img.onerror = () => img.remove();
        container.appendChild(img);
      }

      setTimeout(() => {
        document.getElementById("count").innerText =
          container.children.length;
      }, 150);
    }

    setInterval(refreshFaces, 1000);
 