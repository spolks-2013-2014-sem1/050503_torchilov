Лабораторная работа 2. Знакомство с программированием сокетов.

Простейшая программа­-сервер с использованием протокола TCP (например возвращающая данные клиента). Использование системных утилит (telnet, netcat) в качестве клиента.

Использование:

1. Сомпилировать main.c с помощью gcc:
    gcc main.c -o main
2. Запустить исполняемый файл и передать ему порт, который будет слушать сервер:
    ./main 1234
3. Проверить ответы можно, например, с помощью netcat:
    nc localhost 1234
4. Для разрыва соединения испольнуется комбинация клавиш Ctlr+C, после обрыва возможно повторное подключение по тому же порту.
