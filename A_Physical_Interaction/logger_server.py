from datetime import datetime

from flask import Flask, jsonify, request

app = Flask(__name__)
records = []


@app.get("/health")
def health():
    return jsonify({"status": "ok", "test": "A — Physical Interaction"})


@app.post("/record")
def record():
    data = request.get_json(silent=True) or {}
    item = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "type": data.get("type", "unknown"),
        "content": data.get("content", ""),
    }
    records.append(item)
    print(item)
    return jsonify({"status": "recorded", "record": item})


@app.get("/records")
def get_records():
    return jsonify(records)


@app.delete("/records")
def clear_records():
    records.clear()
    return jsonify({"status": "cleared"})


if __name__ == "__main__":
    print("Love Voice — A Test local event logger")
    print("POST prototype events to http://YOUR_COMPUTER_IP:5000/record")
    app.run(host="0.0.0.0", port=5000, debug=False)
