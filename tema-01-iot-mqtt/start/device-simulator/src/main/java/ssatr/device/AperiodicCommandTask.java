package ssatr.device;

import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 3: implementați taskul aperiodic:
        //   - preluați comenzile din coadă: commands.take()
        //   - separați numele comenzii de parametru: command.split(" ")
        //   - executați comanda (periodic.setPeriodMs(...), sensor.reset() etc.)
        //   - publicați confirmarea pe Config.statusTopic(deviceId)
        //   - o comandă necunoscută sau un parametru invalid nu trebuie să
        //     oprească taskul: publicați "ERROR <motiv>"
        //
        // Vezi exemplul exemple/01-taskuri/.../AperiodicTaskExample.java

        try {
            while (!Thread.currentThread().isInterrupted()) {
                String command = commands.take();
                execute(command);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private void execute(String command) {
        try {
            String[] parts = command.trim().split("\\s+");

            switch (parts[0]) {
                case "SET_INTERVAL" -> {
                    long ms = Long.parseLong(parts[1]);
                    if (ms <= 0) {
                        throw new IllegalArgumentException("intervalul trebuie să fie pozitiv");
                    }
                    periodic.setPeriodMs(ms);
                    reply("OK SET_INTERVAL " + ms);
                }
                case "SET_THRESHOLD" -> {
                    double value = Double.parseDouble(parts[1]);
                    periodic.setThreshold(value);
                    reply("OK SET_THRESHOLD " + value);
                }
                case "RESET" -> {
                    sensor.reset();
                    reply("OK RESET");
                }
                case "STATUS" -> reply("OK STATUS period=" + periodic.getPeriodMs()
                        + " threshold=" + periodic.getThreshold());
                default -> reply("ERROR comanda necunoscuta: " + parts[0]);
            }
        } catch (ArrayIndexOutOfBoundsException e) {
            reply("ERROR lipsește parametrul: " + command);
        } catch (NumberFormatException e) {
            reply("ERROR parametru invalid: " + command);
        } catch (Exception e) {
            reply("ERROR " + e.getMessage());
        }
    }


    private void reply(String text) {
        String msg = deviceId + ";" + System.currentTimeMillis() + ";" + text.replace(';', ',');
        mqtt.publish(Config.statusTopic(deviceId), msg);
        System.out.println("Status: " + msg);
    }
    }

